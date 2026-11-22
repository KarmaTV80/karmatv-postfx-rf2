#pragma once

#include "common.hpp"
#include "core/settings.hpp"
#include "perf/gpu_profiler.hpp"
#include "render/pipeline.hpp"
#include "rules/auto_rules.hpp"
#include "telemetry/rf2_telemetry.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace ktv
{
enum class VariantMode
{
    Auto,
    ForceMonitor,
    ForceVR
};

// Stato globale dell'add-on: un'istanza sola, condivisa da tutti i runtime
// ReShade (monitor e, in VR, quello del visore).
struct App
{
    std::filesystem::path base_dir;    // <Bin64>\KarmaTV-PostFX
    std::filesystem::path presets_dir; // <Bin64>\KarmaTV-PostFX\presets

    Profile profile;     // profilo attivo, modificato dal menu
    int profile_index = 0;
    bool dirty = false;  // modifiche non salvate
    std::string loaded_override; // file di override (pista o auto) caricato, vuoto se nessuno
    bool override_is_car = false; // l'override caricato e' quello per auto

    VariantMode variant_mode = VariantMode::Auto;
    Variant detected_variant = Variant::Monitor;

    bool master_enabled = true;
    bool ui_open = false; // finestra KarmaTV aperta (Bloc Scorr)
    bool split_compare = false;

    Rf2Telemetry telemetry;
    AutoRules rules;
    GpuProfiler profiler;
    EffectPipeline pipeline;

    reshade::api::effect_runtime *primary_runtime = nullptr;

    std::string toast_text;
    double toast_until = 0.0;

    HMODULE module = nullptr; // impostato in DllMain
    // Inizializzazione una sola volta (cartelle, preset), fuori da DllMain.
    void ensure_initialized();
    void init(HMODULE addon_module);
    // Riporta il profilo attivo ai valori predefiniti (non salva).
    void reset_profile_to_defaults();

    // Logica per frame: telemetria, regole, variante, tasti rapidi. Solo sul runtime principale.
    void update_frame(reshade::api::effect_runtime *runtime);

    Variant active_variant() const;
    void set_variant_mode(VariantMode mode);
    const std::vector<std::string> &profile_names() const;
    bool is_builtin_profile(const std::string &name) const;
    void refresh_profiles();
    void save_profile_as(const std::string &name); // nuovo profilo personale con nome
    void delete_current_profile();                 // solo profili personali
    bool ui_text_input = false;                    // si sta scrivendo nel menu: tasti rapidi sospesi

    void load_profile(int index);
    void next_profile(int step);
    enum class SaveTarget
    {
        Profile, // il profilo
        Track,   // solo per la pista corrente
        Car,     // solo per l'auto corrente
        Loaded,  // dove e' stato caricato (override se presente)
    };
    void save_profile(SaveTarget target);

    // Profilo caricato all'avvio del gioco (uno per variante, in avvio.ini)
    std::string startup_profile() const;
    void set_startup_profile();
    // Modifiche non salvate: per i profili fissi = differenze dai valori fissi
    bool modified() const;
    // Lingua del menu (vuota = quella di Windows), salvata in avvio.ini
    void set_language(const std::string &code);

    void toast(std::string text, double seconds = 2.0);
    bool replay_profile_active() const { return replay_switched_; }

private:
    std::vector<std::string> profile_list_;
    Profile builtin_reference_;           // valori del profilo fisso caricato
    std::string startup_monitor_ = "Karma FX";
    std::string startup_vr_ = "VR Karma FX";
    std::string language_;
    void write_startup_file();
    Features saved_system_;               // impostazioni di sistema scritte in sistema.ini
    EffectSetting saved_hdr_;
    double last_system_check_ = 0.0;
    int startup_index() const;
    void load_startup_files();
    void save_system_if_changed(double now);
    bool profile_loaded_once_ = false;
    void update_replay_profile(double now);
    bool replay_switched_ = false; // profilo cambiato per un replay
    bool replay_warned_ = false;
    int profile_before_replay_ = 0;
    double last_replay_seen_ = 0.0;
    bool initialized_ = false;
    double last_frame_ = 0.0;
    double last_telemetry_poll_ = 0.0;
    double last_variant_check_ = 0.0;
    Variant last_variant_ = Variant::Monitor;
};

App &app();
double now_seconds();
// Ferma il controllo di SteamVR in background (alla chiusura del runtime principale)
void stop_vr_watch();
}
