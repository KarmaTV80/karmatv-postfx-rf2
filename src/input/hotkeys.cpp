#include "input/hotkeys.hpp"
#include "app.hpp"
#include "core/log.hpp"
#include "input/wheel.hpp"

#include <array>
#include <cwctype>
#include <fstream>
#include <regex>
#include <sstream>
#include <unordered_map>

// I tasti sono salvati come codice di scansione DirectInput (posizione fisica sulla
// tastiera), lo stesso formato che rF2 usa in Controller.JSON: il confronto con i
// comandi di rF2 e' esatto qualunque sia il layout. Per leggere il tasto da ReShade il
// codice viene tradotto nel tasto virtuale con il layout della finestra di rF2; il nome
// mostrato nel menu usa invece il layout dell'utente (es. italiano).

namespace ktv
{
namespace
{
constexpr size_t kCount = static_cast<size_t>(HotkeyAction::Count);

struct ActionInfo
{
    const char *key;   // chiave nel file tasti.ini
    const char *label; // nome nel menu
    int default_dik;   // posizione fisica predefinita
};

// Predefiniti (tastiera italiana): Canc, +, virgola, punto, meno. Tutti liberi in rF2.
constexpr std::array<ActionInfo, kCount> kActions = {{
    {"finestra", "Apri / chiudi la finestra", 211},
    {"effetti", "Effetti on / off", 27},
    {"profilo_precedente", "Profilo precedente", 51},
    {"profilo_successivo", "Profilo successivo", 52},
    {"confronto", "Confronto A/B", 53},
}};

std::array<int, kCount> g_keys = {};
std::array<wheel::Binding, kCount> g_wheel = {};
std::filesystem::path g_file;
int g_capturing = -1;
int g_capturing_wheel = -1;
std::unordered_map<int, std::string> g_rf2_bindings; // codice -> comando rF2
HKL g_game_layout = nullptr;

HKL game_layout()
{
    return g_game_layout != nullptr ? g_game_layout : GetKeyboardLayout(0);
}

HKL user_layout()
{
    const HWND shell = GetShellWindow();
    return shell != nullptr ? GetKeyboardLayout(GetWindowThreadProcessId(shell, nullptr)) : GetKeyboardLayout(0);
}

UINT dik_to_scan(int dik)
{
    return static_cast<UINT>((dik & 0x7F) | ((dik & 0x80) != 0 ? 0xE000 : 0));
}

unsigned int dik_to_vk(int dik)
{
    switch (dik)
    {
    case 197: return VK_PAUSE;
    case 69: return VK_NUMLOCK;
    default: break;
    }
    return MapVirtualKeyExW(dik_to_scan(dik), MAPVK_VSC_TO_VK_EX, game_layout());
}

int vk_to_dik(unsigned int vk)
{
    switch (vk)
    {
    case VK_PAUSE: return 197;
    case VK_NUMLOCK: return 69;
    default: break;
    }
    const UINT scan = MapVirtualKeyExW(vk, MAPVK_VK_TO_VSC_EX, game_layout());
    if (scan == 0)
        return -1;
    return static_cast<int>((scan & 0x7F) | ((scan & 0xFF00) != 0 ? 0x80 : 0));
}

std::string to_utf8(const wchar_t *text)
{
    char utf8[64] = {};
    WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof(utf8), nullptr, nullptr);
    return utf8;
}

std::string dik_name(int dik)
{
    switch (dik)
    {
    case 0: return "nessuno";
    case 211: return "Canc";
    case 210: return "Ins";
    case 199: return "Inizio";
    case 207: return "Fine";
    case 201: return "PagSu";
    case 209: return "PagGiù";
    case 197: return "Pausa";
    case 70: return "Bloc Scorr";
    case 69: return "Bloc Num";
    case 183: return "Stamp";
    case 57: return "Spazio";
    case 28: return "Invio";
    case 14: return "Backspace";
    case 15: return "Tab";
    default: break;
    }
    if (dik >= 59 && dik <= 68)
        return "F" + std::to_string(dik - 58);
    if (dik == 87 || dik == 88)
        return "F" + std::to_string(dik - 76);

    // Tasti con un carattere: il carattere secondo il layout dell'utente
    const HKL layout = user_layout();
    const UINT scan = dik_to_scan(dik);
    const UINT vk = MapVirtualKeyExW(scan, MAPVK_VSC_TO_VK_EX, layout);
    BYTE state[256] = {};
    wchar_t text[8] = {};
    const int length = ToUnicodeEx(vk, scan & 0xFF, state, text, 7, 0x4 /* non modifica lo stato della tastiera */, layout);
    if (length == 1 && iswprint(text[0]))
    {
        text[0] = static_cast<wchar_t>(towupper(text[0]));
        return to_utf8(text);
    }

    wchar_t name[64] = {};
    const LONG param = static_cast<LONG>(((scan & 0xFF) << 16) | ((scan & 0xFF00) != 0 ? (1 << 24) : 0));
    if (GetKeyNameTextW(param, name, 64) > 0)
        return to_utf8(name);
    return "codice " + std::to_string(dik);
}

void save_hotkeys()
{
    if (g_file.empty())
        return;
    std::ofstream out(g_file, std::ios::trunc);
    out << "; KarmaTV PostFX - tasti rapidi (codici di scansione DirectInput, come in Controller.JSON di rF2)\n";
    for (size_t i = 0; i < kCount; ++i)
    {
        out << kActions[i].key << "_dik=" << g_keys[i] << "\n";
        out << kActions[i].key << "_volante=" << wheel::to_string(g_wheel[i]) << "\n";
    }
}
}

void load_hotkeys(const std::filesystem::path &base_dir)
{
    g_file = base_dir / L"tasti.ini";
    for (size_t i = 0; i < kCount; ++i)
        g_keys[i] = kActions[i].default_dik;

    std::ifstream in(g_file);
    std::string line;
    while (std::getline(in, line))
    {
        const size_t eq = line.find('=');
        if (eq == std::string::npos || line.empty() || line[0] == ';')
            continue;
        const std::string key = line.substr(0, eq);
        for (size_t i = 0; i < kCount; ++i)
        {
            if (key == std::string(kActions[i].key) + "_dik")
            {
                try { g_keys[i] = std::stoi(line.substr(eq + 1)); }
                catch (...) {}
            }
            else if (key == std::string(kActions[i].key) + "_volante")
            {
                g_wheel[i] = wheel::from_string(line.substr(eq + 1));
            }
        }
    }
    save_hotkeys();
}

void handle_hotkeys(reshade::api::effect_runtime *runtime, App &app)
{
    // Layout della finestra di rF2: serve a tradurre le posizioni fisiche in tasti virtuali.
    const HWND window = static_cast<HWND>(runtime->get_hwnd());
    if (window != nullptr)
        g_game_layout = GetKeyboardLayout(GetWindowThreadProcessId(window, nullptr));
    wheel::poll(window);

    // Assegnazione di un pulsante del volante: il primo pulsante premuto (Esc annulla)
    if (g_capturing_wheel >= 0)
    {
        wheel::Binding binding;
        if (runtime->is_key_pressed(VK_ESCAPE))
        {
            g_capturing_wheel = -1;
        }
        else if (wheel::capture(binding))
        {
            g_wheel[static_cast<size_t>(g_capturing_wheel)] = binding;
            save_hotkeys();
            app.toast("Pulsante assegnato: " + wheel::describe(binding) + ". Controlla che rF2 non lo usi.", 5.0);
            g_capturing_wheel = -1;
        }
        return;
    }

    // Assegnazione di un nuovo tasto: il primo tasto premuto (Esc annulla)
    if (g_capturing >= 0)
    {
        if (runtime->is_key_pressed(VK_ESCAPE))
        {
            g_capturing = -1;
            return;
        }
        for (unsigned int vk = 8; vk < 255; ++vk)
        {
            if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LSHIFT || vk == VK_RSHIFT ||
                vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_LMENU || vk == VK_RMENU || vk == VK_LWIN || vk == VK_RWIN)
                continue;
            if (!runtime->is_key_pressed(vk))
                continue;
            const int dik = vk_to_dik(vk);
            if (dik <= 0)
                continue;
            g_keys[static_cast<size_t>(g_capturing)] = dik;
            save_hotkeys();
            const std::string conflict = hotkey_conflict(static_cast<HotkeyAction>(g_capturing));
            app.toast(conflict.empty() ? "Tasto assegnato: " + dik_name(dik)
                                       : "Attenzione: " + dik_name(dik) + " è usato anche da rF2 (" + conflict + ")",
                      conflict.empty() ? 2.0 : 6.0);
            g_capturing = -1;
            break;
        }
        return;
    }

    // Mentre si scrive nel menu (es. il nome di un profilo) i tasti servono al testo
    if (app.ui_text_input)
        return;

    const auto pressed = [&](HotkeyAction action) {
        if (wheel::pressed(g_wheel[static_cast<size_t>(action)]))
            return true;
        const int dik = g_keys[static_cast<size_t>(action)];
        if (dik <= 0)
            return false;
        const unsigned int vk = dik_to_vk(dik);
        return vk != 0 && runtime->is_key_pressed(vk);
    };

    if (pressed(HotkeyAction::ToggleWindow))
        app.ui_open = !app.ui_open;
    else if (app.ui_open && runtime->is_key_pressed(VK_ESCAPE))
        app.ui_open = false;

    if (pressed(HotkeyAction::ToggleEffects))
    {
        app.master_enabled = !app.master_enabled;
        app.toast(app.master_enabled ? "Effetti attivi" : "Effetti spenti");
    }
    if (pressed(HotkeyAction::PreviousProfile))
        app.next_profile(-1);
    if (pressed(HotkeyAction::NextProfile))
        app.next_profile(+1);
    if (pressed(HotkeyAction::Compare))
    {
        app.split_compare = !app.split_compare;
        app.toast(app.split_compare ? "Confronto A/B: a sinistra l'immagine originale" : "Confronto A/B spento");
    }
}

void check_rf2_key_conflicts(const std::filesystem::path &bin64, App &app)
{
    const auto file = bin64.parent_path() / L"UserData" / L"player" / L"Controller.JSON";
    std::ifstream in(file);
    if (!in)
    {
        log::warning("Controller.JSON di rF2 non trovato: controllo dei tasti saltato.");
        return;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();

    // Assegnazioni da tastiera: "Control - Nome":[ 0, codice ] (anche su piu' righe)
    g_rf2_bindings.clear();
    const std::regex binding("\"Control - ([^\"]+)\"\\s*:\\s*\\[\\s*0\\s*,\\s*(\\d+)\\s*\\]");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), binding); it != std::sregex_iterator(); ++it)
        g_rf2_bindings.emplace(std::stoi((*it)[2].str()), (*it)[1].str());

    std::string conflicts;
    for (size_t i = 0; i < kCount; ++i)
    {
        const std::string conflict = hotkey_conflict(static_cast<HotkeyAction>(i));
        if (conflict.empty())
            continue;
        log::warning("Tasto " + dik_name(g_keys[i]) + " (" + kActions[i].label + ") usato anche da rF2: " + conflict);
        conflicts += (conflicts.empty() ? "" : ", ") + dik_name(g_keys[i]);
    }
    if (conflicts.empty())
        log::info("Tasti rapidi KarmaTV: nessun conflitto con i comandi di rF2.");
    else
        app.toast("Attenzione: tasti usati anche da rF2: " + conflicts + ". Cambiali nella pagina Tasti.", 8.0);
}

const char *hotkey_action_label(HotkeyAction action)
{
    return kActions[static_cast<size_t>(action)].label;
}

std::string hotkey_name(HotkeyAction action)
{
    return dik_name(g_keys[static_cast<size_t>(action)]);
}

std::string hotkey_conflict(HotkeyAction action)
{
    const auto it = g_rf2_bindings.find(g_keys[static_cast<size_t>(action)]);
    return it != g_rf2_bindings.end() ? it->second : std::string();
}

void start_hotkey_capture(HotkeyAction action)
{
    g_capturing_wheel = -1;
    g_capturing = static_cast<int>(action);
}

bool hotkey_capturing(HotkeyAction action)
{
    return g_capturing == static_cast<int>(action);
}

void reset_hotkeys()
{
    for (size_t i = 0; i < kCount; ++i)
        g_keys[i] = kActions[i].default_dik;
    save_hotkeys();
}

std::string wheel_button_name(HotkeyAction action)
{
    return wheel::describe(g_wheel[static_cast<size_t>(action)]);
}

void start_wheel_capture(HotkeyAction action)
{
    g_capturing = -1;
    g_capturing_wheel = static_cast<int>(action);
}

bool wheel_capturing(HotkeyAction action)
{
    return g_capturing_wheel == static_cast<int>(action);
}

void clear_wheel_button(HotkeyAction action)
{
    g_wheel[static_cast<size_t>(action)] = wheel::Binding{};
    save_hotkeys();
}

int wheel_device_count()
{
    return wheel::device_count();
}

std::string hotkey_help()
{
    return hotkey_name(HotkeyAction::ToggleWindow) + " finestra   ·   " +
           hotkey_name(HotkeyAction::ToggleEffects) + " effetti on/off   ·   " +
           hotkey_name(HotkeyAction::PreviousProfile) + " / " + hotkey_name(HotkeyAction::NextProfile) + " profilo   ·   " +
           hotkey_name(HotkeyAction::Compare) + " confronto A/B";
}
}
