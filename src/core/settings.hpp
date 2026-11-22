#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ktv
{
enum class Variant
{
    Monitor,
    VR
};

struct EffectSetting
{
    bool enabled = false;
    float intensity = 0.5f; // 0..1, il significato preciso dipende dallo shader
    // Parametri aggiuntivi (nome uniform -> valore); se manca vale il default del catalogo.
    std::unordered_map<std::string, float> params;
    bool operator==(const EffectSetting &) const = default;
};

struct ParamInfo;
// Valore di un parametro: quello del preset o il default del catalogo.
float param_value(const EffectSetting &setting, const ParamInfo &param);

// Funzioni gestite dall'add-on (non shader).
struct Features
{
    bool hud_exclusion = true;
    bool auto_hdr = false;
    float hdr_peak_nits = 1000.0f;
    float hdr_paper_white_nits = 200.0f;
    bool clean_feed = false;
    bool rules_enabled = true;
    bool replay_effects = true;  // effetti anche nei replay (scena a schermo intero)
    bool mirror_grade = true;    // specchietto virtuale con i colori della scena
    bool auto_overrides = true;  // carica da solo i preset salvati per la pista o l'auto corrente
    std::string replay_profile;  // profilo da usare nei replay e nel monitor di gara (vuoto = nessun cambio)
    // N-esimo bind del back buffer dopo la scena principale in cui eseguire gli effetti
    // (0 = fine frame). In rF2: 1 = composizione della scena, 2 = prima di specchietto e HUD.
    int hud_hook_index = 2;
    bool af_override = false;    // filtro anisotropico e LOD forzati sulle texture di rF2
    int af_level = 16;           // 2..16
    float lod_bias = 0.0f;       // negativo = texture piu' nitide in lontananza
    int depth_mode = 0;          // profondita': 0 = automatica, 1 = normale, 2 = invertita
    float depth_far = 1000.0f;   // distanza massima usata per linearizzare la profondita' (m)
    bool operator==(const Features &) const = default;
};

struct Profile
{
    std::string name = "Karma FX";
    Variant variant = Variant::Monitor;
    float budget_ms = 4.0f;
    std::unordered_map<std::string, EffectSetting> effects;
    Features features;
};

const char *variant_name(Variant variant);

// Valori di partenza per ogni profilo e variante, come nella specifica.
Profile make_default_profile(const std::string &name, Variant variant);

// Stesso "aspetto" (effetti e budget), ignorando le impostazioni di sistema e AutoHDR
bool same_look(const Profile &a, const Profile &b);

// Impostazione di un effetto, o quella predefinita (spento) se il preset non la contiene.
EffectSetting effect_setting(const Profile &profile, std::string_view effect_id);

// Nome file: "<Profilo>.<Variante>.ini" oppure "<Profilo>.<Variante>@<Pista>.ini".
std::filesystem::path profile_file_name(const std::string &profile_name, Variant variant, const std::string &track = {});

bool read_profile_file(const std::filesystem::path &file, Profile &out);
bool write_profile_file(const std::filesystem::path &file, const Profile &profile);
}
