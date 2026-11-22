#include "app.hpp"
#include "core/i18n.hpp"
#include "core/log.hpp"
#include "input/hotkeys.hpp"
#include "perf/spike_monitor.hpp"
#include "update/updater.hpp"

#include <TlHelp32.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <cstdio>
#include <cwctype>
#include <fstream>

namespace ktv
{
namespace
{
// Profili integrati (non eliminabili); quelli personali si aggiungono dalla cartella dei preset.
// Profili fissi: si possono provare modifiche, ma si salvano solo come nuovo profilo.
const std::vector<std::string> kMonitorProfiles = {"Originale rF2", "Karma FX", "Replay Karma FX"};
const std::vector<std::string> kVrProfiles = {"Originale rF2", "VR Karma FX"};

std::string clean_profile_name(std::string name)
{
    // '@' separa gli override per pista nel nome del file: non e' ammesso nei nomi
    for (char &c : name)
        if (c == '@' || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*')
            c = '-';
    const size_t first = name.find_first_not_of(' ');
    const size_t last = name.find_last_not_of(' ');
    return first == std::string::npos ? std::string() : name.substr(first, last - first + 1);
}

std::atomic<bool> g_vr_running{false};
std::thread *g_vr_thread = nullptr; // puntatore: a fine processo non serve distruggerlo
std::mutex g_vr_mutex;
std::condition_variable g_vr_wake;
bool g_vr_stop = false;

bool process_running(const wchar_t *exe_name)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return false;
    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    bool found = false;
    for (BOOL ok = Process32FirstW(snapshot, &entry); ok && !found; ok = Process32NextW(snapshot, &entry))
        found = _wcsicmp(entry.szExeFile, exe_name) == 0;
    CloseHandle(snapshot);
    return found;
}

bool launched_with_vr_flag()
{
    std::wstring command_line = GetCommandLineW();
    for (wchar_t &c : command_line)
        c = static_cast<wchar_t>(towlower(c));
    return command_line.find(L"+vr") != std::wstring::npos;
}

// rF2 carica openvr_api.dll anche su monitor, quindi non basta come indizio.
// VR = avvio con +VR oppure SteamVR (vrserver.exe) in esecuzione.
// La variante si puo' comunque forzare dal menu.
bool detect_vr()
{
    static const bool vr_flag = launched_with_vr_flag();
    if (vr_flag)
        return true;
    // L'elenco dei processi di Windows costa diversi ms: fatto sul thread di gioco una volta
    // al secondo dava un frame lento al secondo. Ora lo aggiorna un thread a parte ogni 2 s;
    // solo la prima volta (all'avvio) si controlla subito.
    if (g_vr_thread == nullptr)
    {
        g_vr_running = process_running(L"vrserver.exe");
        g_vr_stop = false;
        g_vr_thread = new std::thread([] {
            std::unique_lock<std::mutex> lock(g_vr_mutex);
            while (!g_vr_stop)
            {
                if (g_vr_wake.wait_for(lock, std::chrono::seconds(2), [] { return g_vr_stop; }))
                    break;
                lock.unlock();
                g_vr_running = process_running(L"vrserver.exe");
                lock.lock();
            }
        });
    }
    return g_vr_running;
}

// Impostazioni "di sistema" (AutoHDR, esclusione HUD, filtro anisotropico, profondita',
// automatismi): non fanno parte del look e restano le stesse cambiando profilo.
void carry_system_settings(Profile &target, const Profile &source)
{
    target.features = source.features;
    const auto hdr = source.effects.find("auto_hdr");
    if (hdr != source.effects.end())
        target.effects["auto_hdr"] = hdr->second;
}

// Override per auto: stesso schema dei file per pista, con il prefisso "auto="
std::string car_key(const std::string &vehicle)
{
    return "auto=" + vehicle;
}

std::string timestamp()
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%04u%02u%02u-%02u%02u%02u",
                  t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return buffer;
}
}

App &app()
{
    static App instance;
    return instance;
}

double now_seconds()
{
    static const double frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart);
    }();
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) / frequency;
}

void App::ensure_initialized()
{
    if (initialized_)
        return;
    initialized_ = true;
    init(module);
}

void App::reset_profile_to_defaults()
{
    const std::string name = profile_names()[profile_index];
    const Profile current = profile;
    profile = make_default_profile(name, active_variant());
    carry_system_settings(profile, current);
    dirty = true;
    toast(tr("Valori predefiniti ripristinati: salva per mantenerli"));
}

void App::init(HMODULE addon_module)
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(addon_module, path, MAX_PATH);
    base_dir = std::filesystem::path(path).parent_path() / L"KarmaTV-PostFX";
    presets_dir = base_dir / L"presets";

    std::error_code ec;
    std::filesystem::create_directories(presets_dir / L"backup", ec);
    if (ec)
        log::error("Impossibile creare la cartella dei preset: " + presets_dir.string());

    detected_variant = detect_vr() ? Variant::VR : Variant::Monitor;
    last_variant_ = active_variant();
    refresh_profiles();
    load_startup_files();
    i18n::set_language(language_);
    load_profile(startup_index());
    load_hotkeys(base_dir);
    check_rf2_key_conflicts(base_dir.parent_path(), *this);
    log::info("Avviato. Variante: " + std::string(variant_name(active_variant())) + ", profilo: " + profile.name);
}

Variant App::active_variant() const
{
    switch (variant_mode)
    {
    case VariantMode::ForceMonitor: return Variant::Monitor;
    case VariantMode::ForceVR: return Variant::VR;
    default: return detected_variant;
    }
}

void App::set_variant_mode(VariantMode mode)
{
    variant_mode = mode;
    if (active_variant() != last_variant_)
    {
        last_variant_ = active_variant();
        refresh_profiles();
        load_profile(startup_index());
    }
}

const std::vector<std::string> &App::profile_names() const
{
    return profile_list_;
}

bool App::is_builtin_profile(const std::string &name) const
{
    const auto &builtins = active_variant() == Variant::VR ? kVrProfiles : kMonitorProfiles;
    return std::find(builtins.begin(), builtins.end(), name) != builtins.end();
}

void App::refresh_profiles()
{
    const Variant variant = active_variant();
    profile_list_ = variant == Variant::VR ? kVrProfiles : kMonitorProfiles;

    // Profili personali: file "<Nome>.<Variante>.ini" (senza '@', che indica un override per pista)
    std::vector<std::string> custom;
    std::error_code ec;
    const std::string suffix = std::string(".") + variant_name(variant);
    for (const auto &entry : std::filesystem::directory_iterator(presets_dir, ec))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".ini")
            continue;
        const std::string stem = entry.path().stem().string();
        if (stem.find('@') != std::string::npos || stem.size() <= suffix.size() ||
            stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) != 0)
            continue;
        const std::string name = stem.substr(0, stem.size() - suffix.size());
        if (!is_builtin_profile(name))
            custom.push_back(name);
    }
    std::sort(custom.begin(), custom.end());
    profile_list_.insert(profile_list_.end(), custom.begin(), custom.end());
}

void App::save_profile_as(const std::string &requested_name)
{
    const std::string name = clean_profile_name(requested_name);
    if (name.empty())
    {
        toast(tr("Scrivi un nome per il nuovo profilo"));
        return;
    }
    const Variant variant = active_variant();
    profile.name = name;
    profile.variant = variant;
    const auto file = presets_dir / profile_file_name(name, variant);
    if (!write_profile_file(file, profile))
    {
        toast(tr("Errore nel salvataggio del profilo"));
        return;
    }
    refresh_profiles();
    const auto it = std::find(profile_list_.begin(), profile_list_.end(), name);
    profile_index = it != profile_list_.end() ? static_cast<int>(it - profile_list_.begin()) : 0;
    loaded_override.clear();
    dirty = false;
    toast(tr("Profilo salvato: ") + name);
}

void App::delete_current_profile()
{
    const std::string name = profile_list_[profile_index];
    if (is_builtin_profile(name))
    {
        toast(tr("I profili fissi non si possono eliminare"));
        return;
    }
    std::error_code ec;
    std::filesystem::remove(presets_dir / profile_file_name(name, active_variant()), ec);
    refresh_profiles();
    load_profile(startup_index());
    toast(tr("Profilo eliminato: ") + name);
}

void App::load_profile(int index)
{
    if (profile_list_.empty())
        refresh_profiles();
    const auto &names = profile_names();
    const int count = static_cast<int>(names.size());
    profile_index = ((index % count) + count) % count;
    const std::string &name = names[profile_index];
    const Variant variant = active_variant();

    loaded_override.clear();
    override_is_car = false;
    Profile loaded;

    // 1. Override per la pista corrente, se esiste; 1b. altrimenti per l'auto corrente.
    //    Si possono disattivare ("Preset automatici per pista e auto" nel menu).
    const TelemetrySnapshot &t = telemetry.snapshot();
    const bool builtin = is_builtin_profile(name);
    const bool use_overrides = profile.features.auto_overrides && !builtin;
    if (use_overrides && t.connected && !t.track_name.empty())
    {
        const auto file = presets_dir / profile_file_name(name, variant, t.track_name);
        if (std::filesystem::exists(file) && read_profile_file(file, loaded))
            loaded_override = file.filename().string();
    }
    if (use_overrides && loaded_override.empty() && t.connected && !t.vehicle_name.empty())
    {
        const auto file = presets_dir / profile_file_name(name, variant, car_key(t.vehicle_name));
        if (std::filesystem::exists(file) && read_profile_file(file, loaded))
        {
            loaded_override = file.filename().string();
            override_is_car = true;
        }
    }

    // 2. Preset del profilo; se manca viene creato con i valori di partenza.
    if (builtin)
    {
        loaded = make_default_profile(name, variant); // fisso: sempre i valori di partenza
    }
    else if (loaded_override.empty())
    {
        const auto file = presets_dir / profile_file_name(name, variant);
        if (!std::filesystem::exists(file) || !read_profile_file(file, loaded))
        {
            loaded = make_default_profile(name, variant);
            write_profile_file(file, loaded);
            log::info("Creato il preset predefinito " + file.filename().string());
        }
    }

    loaded.name = name;
    loaded.variant = variant;
    // Dal secondo caricamento in poi le impostazioni di sistema seguono quelle in uso
    // (es. AutoHDR acceso resta acceso anche passando a un profilo salvato senza).
    if (profile_loaded_once_)
        carry_system_settings(loaded, profile);
    profile_loaded_once_ = true;
    profile = std::move(loaded);
    builtin_reference_ = builtin ? profile : Profile{};
    dirty = false;
    spikes::note("profilo " + profile.name);
}

void App::next_profile(int step)
{
    load_profile(profile_index + step);
    toast(tr("Profilo: ") + profile.name + " (" + variant_name(profile.variant) + ")");
}

void App::save_profile(SaveTarget target)
{
    const std::string &name = profile_names()[profile_index];
    if (is_builtin_profile(name))
    {
        toast(tr("Profilo fisso: salva le modifiche come nuovo profilo"), 3.0);
        return;
    }
    const Variant variant = active_variant();
    if (target == SaveTarget::Loaded)
        target = loaded_override.empty() ? SaveTarget::Profile : (override_is_car ? SaveTarget::Car : SaveTarget::Track);
    const TelemetrySnapshot &t = telemetry.snapshot();
    std::string track;
    if (target == SaveTarget::Track)
    {
        track = t.track_name;
        if (track.empty())
        {
            toast(tr("Nessuna pista caricata: override non salvato"));
            return;
        }
    }
    else if (target == SaveTarget::Car)
    {
        if (t.vehicle_name.empty())
        {
            toast(tr("Nessuna auto in pista: override non salvato"));
            return;
        }
        track = car_key(t.vehicle_name);
    }
    const bool as_override = !track.empty();

    profile.name = name;
    profile.variant = variant;
    const auto file = presets_dir / profile_file_name(name, variant, track);
    if (!write_profile_file(file, profile))
    {
        toast(tr("Errore nel salvataggio del preset"));
        return;
    }

    // Copia datata per poter tornare indietro.
    const auto backup = presets_dir / L"backup" / (file.stem().string() + "_" + timestamp() + ".ini");
    write_profile_file(backup, profile);

    if (as_override)
    {
        loaded_override = file.filename().string();
        override_is_car = target == SaveTarget::Car;
    }
    dirty = false;
    toast(tr("Preset salvato: ") + file.filename().string());
}

// Replay e monitor di gara a schermo intero: si passa al profilo scelto e, finito il replay,
// si torna a quello di prima. L'uscita aspetta 2 secondi per non saltare ai cambi di camera.
void App::update_replay_profile(double now)
{
    const bool replay = pipeline.replay_detected();
    if (replay)
        last_replay_seen_ = now;

    if (!replay_switched_)
    {
        const std::string target = profile.features.replay_profile;
        if (!replay || target.empty() || profile.name == target)
            return;
        const auto &names = profile_names();
        const auto it = std::find(names.begin(), names.end(), target);
        if (it == names.end())
            return;
        if (modified())
        {
            if (!replay_warned_)
                toast(tr("Replay: modifiche non salvate, il profilo non cambia"), 4.0);
            replay_warned_ = true;
            return;
        }
        profile_before_replay_ = profile_index;
        replay_switched_ = true;
        load_profile(static_cast<int>(it - names.begin()));
        toast(tr("Replay: profilo ") + profile.name);
        return;
    }

    if (now - last_replay_seen_ < 2.0)
        return;
    replay_switched_ = false;
    replay_warned_ = false;
    if (!modified())
    {
        load_profile(profile_before_replay_);
        toast(tr("Fine replay: profilo ") + profile.name);
    }
}

bool App::modified() const
{
    if (!profile_list_.empty() && is_builtin_profile(profile_list_[profile_index]))
        return !same_look(profile, builtin_reference_);
    return dirty;
}

std::string App::startup_profile() const
{
    return active_variant() == Variant::VR ? startup_vr_ : startup_monitor_;
}

int App::startup_index() const
{
    const auto &names = profile_list_;
    const auto find = [&names](const std::string &n) {
        const auto it = std::find(names.begin(), names.end(), n);
        return it != names.end() ? static_cast<int>(it - names.begin()) : -1;
    };
    int index = find(startup_profile());
    if (index < 0)
        index = find(active_variant() == Variant::VR ? "VR Karma FX" : "Karma FX");
    return std::max(index, 0);
}

void App::set_startup_profile()
{
    const std::string name = profile_list_[profile_index];
    (active_variant() == Variant::VR ? startup_vr_ : startup_monitor_) = name;
    write_startup_file();
    toast(tr("All'avvio: ") + name);
}

void App::write_startup_file()
{
    std::ofstream out(base_dir / L"avvio.ini");
    out << "; KarmaTV PostFX - profilo caricato all'avvio di rF2 e lingua del menu\n";
    out << "Monitor=" << startup_monitor_ << "\n";
    out << "VR=" << startup_vr_ << "\n";
    out << "Lingua=" << language_ << "\n";
    if (!out)
        toast(tr("Errore nel salvataggio di avvio.ini"));
}

void App::set_language(const std::string &code)
{
    language_ = code;
    i18n::set_language(code);
    write_startup_file();
}

// avvio.ini: profilo di avvio per Monitor e VR
void App::load_startup_files()
{
    std::ifstream in(base_dir / L"avvio.ini");
    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.rfind("Monitor=", 0) == 0 && line.size() > 8)
            startup_monitor_ = line.substr(8);
        else if (line.rfind("VR=", 0) == 0 && line.size() > 3)
            startup_vr_ = line.substr(3);
        else if (line.rfind("Lingua=", 0) == 0)
            language_ = line.substr(7);
    }
}

// sistema.ini: impostazioni di sistema, uguali per tutti i profili (AutoHDR, esclusione HUD,
// filtro anisotropico, profondita', automatismi...). Si salvano da sole quando cambiano,
// cosi' valgono anche con i profili fissi, che non si sovrascrivono.
void App::save_system_if_changed(double now)
{
    if (now - last_system_check_ < 1.0)
        return;
    const bool first = last_system_check_ == 0.0;
    last_system_check_ = now;
    const auto hdr_of = [](const Profile &p) { return effect_setting(p, "auto_hdr"); };
    const auto file = base_dir / L"sistema.ini";
    if (first)
    {
        Profile system;
        if (read_profile_file(file, system))
        {
            carry_system_settings(profile, system);
            if (!builtin_reference_.effects.empty())
                carry_system_settings(builtin_reference_, system);
            saved_system_ = profile.features;
            saved_hdr_ = hdr_of(profile);
            return;
        }
    }
    else if (profile.features == saved_system_ && hdr_of(profile) == saved_hdr_)
    {
        return;
    }
    Profile system;
    system.name = "sistema";
    carry_system_settings(system, profile);
    if (write_profile_file(file, system))
    {
        saved_system_ = profile.features;
        saved_hdr_ = hdr_of(profile);
    }
}

void App::toast(std::string text, double seconds)
{
    toast_text = std::move(text);
    toast_until = now_seconds() + seconds;
}

void App::update_frame(reshade::api::effect_runtime *runtime)
{
    const double now = now_seconds();
    const float dt = last_frame_ > 0.0 ? static_cast<float>(now - last_frame_) : 0.0f;
    last_frame_ = now;
    spikes::cpu_frame(now, dt);

    // Telemetria 4 volte al secondo; cambio di pista = ricarica del profilo (override).
    if (now - last_telemetry_poll_ > 0.25)
    {
        last_telemetry_poll_ = now;
        const std::string previous_track = telemetry.snapshot().track_name;
        const std::string previous_vehicle = telemetry.snapshot().vehicle_name;
        telemetry.poll();
        const TelemetrySnapshot &t = telemetry.snapshot();
        const bool track_changed = t.track_name != previous_track && !t.track_name.empty();
        const bool vehicle_changed = t.vehicle_name != previous_vehicle && !t.vehicle_name.empty();
        if (t.connected && (track_changed || vehicle_changed))
        {
            if (modified())
            {
                toast(tr("Pista o auto cambiata: salva o ricarica il profilo per applicare gli override"), 4.0);
            }
            else
            {
                load_profile(profile_index);
                if (!loaded_override.empty())
                    toast(tr("Override caricato: ") + loaded_override);
            }
        }
    }

    telemetry.poll_camera();

    // Variante Monitor/VR: controllo una volta al secondo.
    if (now - last_variant_check_ > 1.0)
    {
        last_variant_check_ = now;
        detected_variant = detect_vr() ? Variant::VR : Variant::Monitor;
        if (active_variant() != last_variant_)
        {
            last_variant_ = active_variant();
            refresh_profiles();
            load_profile(startup_index());
            toast(std::string(tr("Variante ")) + variant_name(last_variant_) + tr(": profilo ") + profile.name);
        }
    }

    save_system_if_changed(now);

    // Aggiornamenti: un controllo 20 secondi dopo l'avvio, poi un avviso se c'e' una versione nuova
    static double first_frame = now;
    if (now - first_frame > 20.0)
        update::check();
    static bool announced = false;
    if (!announced)
    {
        const update::Status u = update::status();
        if (u.state == update::State::Available)
        {
            announced = true;
            toast(tr("KarmaTV PostFX ") + u.version + tr(" disponibile: apri il menu (Canc) per aggiornare"), 8.0);
        }
    }
    rules.update(telemetry.snapshot(), dt, profile.features.rules_enabled);
    update_replay_profile(now);
    handle_hotkeys(runtime, *this);
}

void stop_vr_watch()
{
    {
        const std::lock_guard<std::mutex> lock(g_vr_mutex);
        g_vr_stop = true;
    }
    g_vr_wake.notify_all();
    if (g_vr_thread != nullptr)
    {
        g_vr_thread->join();
        delete g_vr_thread;
        g_vr_thread = nullptr;
    }
}
}
