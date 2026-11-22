#include "core/settings.hpp"
#include "core/effects_catalog.hpp"
#include "core/log.hpp"

#include <charconv>
#include <fstream>
#include <sstream>

namespace ktv
{
namespace
{
std::string_view trim(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
        s.remove_suffix(1);
    return s;
}

// from_chars/to_chars non dipendono dalla locale (rF2 potrebbe impostarne una con la virgola).
bool parse_float(std::string_view s, float &out)
{
    const auto result = std::from_chars(s.data(), s.data() + s.size(), out);
    return result.ec == std::errc{};
}

std::string format_float(float value)
{
    char buffer[32];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::fixed, 3);
    return std::string(buffer, result.ptr);
}

bool parse_bool(std::string_view s)
{
    return s == "1" || s == "true" || s == "yes";
}

std::string sanitize_file_part(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (const char c : s)
    {
        const bool invalid = c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*' || static_cast<unsigned char>(c) < 32;
        out.push_back(invalid ? '_' : c);
    }
    return out;
}
}

namespace
{
bool read_profile_stream(std::istream &in, Profile &out);

// Profilo "Karma FX": regolato in gioco dall'utente il 22/11/2026 e reso fisso.
// "Replay Karma FX" e "VR Karma FX" partono dagli stessi valori (vedi make_default_profile).
constexpr const char kKarmaFxPreset[] = R"KTV(; KarmaTV PostFX - preset
[profile]
name=Karma FX
variant=Monitor
budget_ms=6.000

[features]
hud_exclusion=1
auto_hdr=0
hdr_peak_nits=1000.000
hdr_paper_white_nits=200.000
clean_feed=0
rules_enabled=0
replay_effects=1
mirror_grade=1
auto_overrides=1
replay_profile=Replay Karma FX
hud_hook_index=2
depth_mode=0
depth_far=1000.000
af_override=1
af_level=16
lod_bias=0.000

[effects]
tonemap.enabled=1
tonemap.intensity=0.350
tonemap.p.KTV_Mode=0.000
tonemap.p.KTV_Exposure=0.050
tonemap.p.KTV_HdrRecovery=0.950
tonemap.p.KTV_AutoExposure=0.500
tonemap.p.KTV_AdaptSpeed=1.500
tonemap.p.KTV_DashEV=0.600
tonemap.p.KTV_DashRecovery=-0.010
tonemap.p.KTV_CockpitEV=2.000
tonemap.p.KTV_CockpitRecovery=0.350
tonemap.p.KTV_TrackEV=-2.000
tonemap.p.KTV_TrackRecovery=0.000
tonemap.p.KTV_FarEV=0.000
tonemap.p.KTV_FarRecovery=0.000
tonemap.p.KTV_ShowZones=0.000
tonemap.p.KTV_DashCenter=0.500
tonemap.p.KTV_DashWidth=0.350
tonemap.p.KTV_DashTop=0.470
tonemap.p.KTV_DashBottom=1.000
tonemap.p.KTV_CockpitDistance=7.250
tonemap.p.KTV_FarDistance=2000.000
bloom.enabled=0
bloom.intensity=0.850
bloom.p.KTV_Threshold=0.800
bloom.p.KTV_Knee=0.200
auto_hdr.enabled=0
auto_hdr.intensity=0.500
auto_hdr.p.KTV_PeakNits=1000.000
auto_hdr.p.KTV_PaperWhite=200.000
auto_hdr.p.KTV_Saturation=1.000
star_glare.enabled=0
star_glare.intensity=0.500
star_glare.p.KTV_GlareThreshold=0.850
star_glare.p.KTV_GlareLength=1.000
grade.enabled=1
grade.intensity=1.000
grade.p.KTV_Temperature=-0.150
grade.p.KTV_Tint=-0.100
grade.p.KTV_Vibrance=0.050
grade.p.KTV_Saturation=1.000
grade.p.KTV_Contrast=0.450
grade.p.KTV_LutIndex=6.000
grade.p.KTV_LutAmount=1.000
cas.enabled=1
cas.intensity=0.850
fsr1.enabled=1
fsr1.intensity=1.000
fsr1.p.KTV_RcasDenoise=0.100
smaa.enabled=0
smaa.intensity=0.850
smaa.p.KTV_AAThreshold=1.000
gtao.enabled=1
gtao.intensity=0.900
gtao.p.KTV_AORadius=0.700
gtao.p.KTV_AOFovY=40.000
gtao.p.KTV_AOMaxDistance=250.000
sky.enabled=1
sky.intensity=0.500
sky.p.KTV_SkyExposure=-0.500
sky.p.KTV_SkySaturation=1.200
sky.p.KTV_SkyBlue=0.800
sky.p.KTV_SkyContrast=0.300
sky.p.KTV_SkyHorizon=0.400
fog.enabled=1
fog.intensity=0.700
fog.p.KTV_Visibility=3500.000
fog.p.KTV_SkyAmount=0.100
fog.p.KTV_FogTint=0.000
fog.p.KTV_FogBrightness=0.850
dof.enabled=0
dof.intensity=0.200
dof.p.KTV_FocusRange=2.000
dof.p.KTV_FocusSpeed=3.000
rtgi.enabled=1
rtgi.intensity=0.900
rtgi.p.KTV_GIRadius=1.500
rtgi.p.KTV_GIShadowsOnly=0.700
rtgi.p.KTV_GIFovY=40.000
lens_extras.enabled=0
lens_extras.intensity=0.000
lens_extras.p.KTV_Aberration=0.500
lens_extras.p.KTV_Grain=0.400
lens_extras.p.KTV_Dirt=0.400
motion_blur.enabled=0
motion_blur.intensity=0.500
motion_blur.p.KTV_MaxBlur=3.000
debug_views.enabled=0
debug_views.intensity=0.500
debug_views.p.KTV_DebugMode=0.000
)KTV";
}

float param_value(const EffectSetting &setting, const ParamInfo &param)
{
    const auto it = setting.params.find(param.uniform);
    return it != setting.params.end() ? it->second : param.default_value;
}

const char *variant_name(Variant variant)
{
    return variant == Variant::VR ? "VR" : "Monitor";
}

Profile make_default_profile(const std::string &name, Variant variant)
{
    Profile p;
    p.name = name;
    p.variant = variant;

    const bool vr = variant == Variant::VR;

    // Tutti gli effetti shader presenti nel preset, spenti.
    for (const EffectInfo &effect : effect_catalog())
        if (effect.fx_file != nullptr)
            p.effects[effect.id] = EffectSetting{false, 0.5f};

    const auto on = [&p](const char *id, float intensity) { p.effects[id] = EffectSetting{true, intensity}; };

    // Profili fissi Karma FX: valori dal testo salvato
    if (name == "Karma FX" || name == "Replay Karma FX" || name == "VR Karma FX")
    {
        std::istringstream text(kKarmaFxPreset);
        Profile preset;
        if (read_profile_stream(text, preset))
        {
            preset.name = name;
            preset.variant = variant;
            if (name == "Replay Karma FX")
            {
                // Regia: profondita' di campo, motion blur e antialiasing in piu'
                preset.effects["smaa"].enabled = true;
                preset.effects["dof"] = EffectSetting{true, 0.25f, preset.effects["dof"].params};
                preset.effects["dof"].params["KTV_FocusSpeed"] = 5.05f;
                preset.effects["motion_blur"] = EffectSetting{true, 0.75f, preset.effects["motion_blur"].params};
            }
            return preset;
        }
    }

    // Grafica originale di rF2: nessun effetto, nessun automatismo che ne accenda
    if (name == "Originale rF2")
    {
        p.features.rules_enabled = false;
        p.budget_ms = vr ? 2.0f : 4.0f;
        return p;
    }

    // Pacchetto Gara (base di tutti i profili).
    // Valori pensati per essere visibili: a 1.0 l'effetto e' marcato.
    on("tonemap", 1.0f); // Neutro: a esposizione 0 l'immagine non cambia
    on("bloom", vr ? 0.25f : 0.3f);
    on("grade", 1.0f);
    on("cas", vr ? 0.5f : 0.6f);
    on("fog", 0.5f);
    on("gtao", vr ? 0.4f : 0.7f);
    // AutoHDR resta spento: si attiva dal menu e vale dal riavvio di rF2.
    p.budget_ms = vr ? 2.0f : 4.0f;
    p.features.auto_hdr = !vr;

    return p;
}

bool same_look(const Profile &a, const Profile &b)
{
    if (a.budget_ms != b.budget_ms)
        return false;
    const auto covered = [](const Profile &x, const Profile &y) {
        for (const auto &[id, setting] : x.effects)
            if (id != "auto_hdr" && !(effect_setting(y, id) == setting))
                return false;
        return true;
    };
    return covered(a, b) && covered(b, a);
}

EffectSetting effect_setting(const Profile &profile, std::string_view effect_id)
{
    const auto it = profile.effects.find(std::string(effect_id));
    return it != profile.effects.end() ? it->second : EffectSetting{};
}

std::filesystem::path profile_file_name(const std::string &profile_name, Variant variant, const std::string &track)
{
    std::string file = sanitize_file_part(profile_name) + "." + variant_name(variant);
    if (!track.empty())
        file += "@" + sanitize_file_part(track);
    file += ".ini";
    return std::filesystem::path(file);
}

bool read_profile_file(const std::filesystem::path &file, Profile &out)
{
    std::ifstream in(file);
    if (!in)
        return false;
    return read_profile_stream(in, out);
}

namespace
{
bool read_profile_stream(std::istream &in, Profile &out)
{
    Profile p;
    std::string section;
    std::string raw;
    while (std::getline(in, raw))
    {
        const std::string_view line = trim(raw);
        if (line.empty() || line.front() == ';' || line.front() == '#')
            continue;
        if (line.front() == '[' && line.back() == ']')
        {
            section = std::string(line.substr(1, line.size() - 2));
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string_view::npos)
            continue;
        const std::string_view key = trim(line.substr(0, eq));
        const std::string_view value = trim(line.substr(eq + 1));

        if (section == "profile")
        {
            if (key == "name") p.name = std::string(value);
            else if (key == "variant") p.variant = (value == "VR") ? Variant::VR : Variant::Monitor;
            else if (key == "budget_ms") parse_float(value, p.budget_ms);
        }
        else if (section == "features")
        {
            if (key == "hud_exclusion") p.features.hud_exclusion = parse_bool(value);
            else if (key == "auto_hdr") p.features.auto_hdr = parse_bool(value);
            else if (key == "hdr_peak_nits") parse_float(value, p.features.hdr_peak_nits);
            else if (key == "hdr_paper_white_nits") parse_float(value, p.features.hdr_paper_white_nits);
            else if (key == "clean_feed") p.features.clean_feed = parse_bool(value);
            else if (key == "rules_enabled") p.features.rules_enabled = parse_bool(value);
            else if (key == "replay_effects") p.features.replay_effects = parse_bool(value);
            else if (key == "mirror_grade") p.features.mirror_grade = parse_bool(value);
            else if (key == "auto_overrides") p.features.auto_overrides = parse_bool(value);
            else if (key == "replay_profile") p.features.replay_profile = std::string(value);
            else if (key == "hud_hook_index")
            {
                float number = 0.0f;
                if (parse_float(value, number))
                    p.features.hud_hook_index = static_cast<int>(number);
            }
            else if (key == "depth_mode")
            {
                float number = 0.0f;
                if (parse_float(value, number))
                    p.features.depth_mode = static_cast<int>(number);
            }
            else if (key == "depth_far") parse_float(value, p.features.depth_far);
            else if (key == "af_override") p.features.af_override = parse_bool(value);
            else if (key == "af_level")
            {
                float number = 16.0f;
                if (parse_float(value, number))
                    p.features.af_level = static_cast<int>(number);
            }
            else if (key == "lod_bias") parse_float(value, p.features.lod_bias);
        }
        else if (section == "effects")
        {
            // Formato: <id>.enabled=1 / <id>.intensity=0.500 / <id>.p.<Uniform>=1.000
            const size_t dot = key.find('.');
            if (dot == std::string_view::npos)
                continue;
            const std::string id(key.substr(0, dot));
            const std::string_view field = key.substr(dot + 1);
            if (find_effect(id) == nullptr)
                continue; // voce sconosciuta (preset di una versione diversa)
            EffectSetting &setting = p.effects[id];
            if (field == "enabled") setting.enabled = parse_bool(value);
            else if (field == "intensity") parse_float(value, setting.intensity);
            else if (field.size() > 2 && field.substr(0, 2) == "p.")
            {
                float number = 0.0f;
                if (parse_float(value, number))
                    setting.params[std::string(field.substr(2))] = number;
            }
        }
    }

    out = std::move(p);
    return true;
}
}

bool write_profile_file(const std::filesystem::path &file, const Profile &profile)
{
    std::ofstream out(file, std::ios::trunc);
    if (!out)
    {
        log::error("Impossibile scrivere il preset " + file.string());
        return false;
    }

    out << "; KarmaTV PostFX - preset\n";
    out << "[profile]\n";
    out << "name=" << profile.name << "\n";
    out << "variant=" << variant_name(profile.variant) << "\n";
    out << "budget_ms=" << format_float(profile.budget_ms) << "\n\n";

    out << "[features]\n";
    out << "hud_exclusion=" << (profile.features.hud_exclusion ? 1 : 0) << "\n";
    out << "auto_hdr=" << (profile.features.auto_hdr ? 1 : 0) << "\n";
    out << "hdr_peak_nits=" << format_float(profile.features.hdr_peak_nits) << "\n";
    out << "hdr_paper_white_nits=" << format_float(profile.features.hdr_paper_white_nits) << "\n";
    out << "clean_feed=" << (profile.features.clean_feed ? 1 : 0) << "\n";
    out << "rules_enabled=" << (profile.features.rules_enabled ? 1 : 0) << "\n";
    out << "replay_effects=" << (profile.features.replay_effects ? 1 : 0) << "\n";
    out << "mirror_grade=" << (profile.features.mirror_grade ? 1 : 0) << "\n";
    out << "auto_overrides=" << (profile.features.auto_overrides ? 1 : 0) << "\n";
    out << "replay_profile=" << profile.features.replay_profile << "\n";
    out << "hud_hook_index=" << profile.features.hud_hook_index << "\n";
    out << "depth_mode=" << profile.features.depth_mode << "\n";
    out << "depth_far=" << format_float(profile.features.depth_far) << "\n";
    out << "af_override=" << (profile.features.af_override ? 1 : 0) << "\n";
    out << "af_level=" << profile.features.af_level << "\n";
    out << "lod_bias=" << format_float(profile.features.lod_bias) << "\n\n";

    out << "[effects]\n";
    // Ordine del catalogo, cosi' i file restano leggibili e confrontabili.
    for (const EffectInfo &effect : effect_catalog())
    {
        const auto it = profile.effects.find(effect.id);
        if (it == profile.effects.end())
            continue;
        out << effect.id << ".enabled=" << (it->second.enabled ? 1 : 0) << "\n";
        out << effect.id << ".intensity=" << format_float(it->second.intensity) << "\n";
        for (const ParamInfo &param : effect.params)
            out << effect.id << ".p." << param.uniform << "=" << format_float(param_value(it->second, param)) << "\n";
    }
    return static_cast<bool>(out);
}
}
