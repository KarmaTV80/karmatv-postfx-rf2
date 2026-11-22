#include "core/effects_catalog.hpp"

#include <array>

namespace ktv
{
namespace
{
using C = Category;

// Parametri aggiuntivi regolabili dalla finestra KarmaTV (stessi nomi delle uniform negli shader).
constexpr ParamInfo kTonemapParams[] = {
    {"KTV_Mode", "Tonemapper", 0.0f, 2.0f, 0.0f, "Neutro\0ACES\0AgX\0", "Generale"},
    {"KTV_Exposure", "Esposizione (EV)", -2.0f, 2.0f, 0.0f, nullptr},
    {"KTV_HdrRecovery", "Recupero alte luci", 0.0f, 0.95f, 0.6f, nullptr},
    {"KTV_AutoExposure", "Esposizione automatica", 0.0f, 1.0f, 0.5f, nullptr},
    {"KTV_AdaptSpeed", "Velocità di adattamento", 0.1f, 5.0f, 1.5f, nullptr},
    {"KTV_DashEV", "Esposizione (EV)", -2.0f, 2.0f, 0.0f, nullptr, "Cruscotto (strumenti, volante, plancia)"},
    {"KTV_DashRecovery", "Alte luci", -0.6f, 0.35f, 0.0f, nullptr},
    {"KTV_CockpitEV", "Esposizione (EV)", -2.0f, 2.0f, 0.0f, nullptr, "Abitacolo (montanti, tetto, fianchi)"},
    {"KTV_CockpitRecovery", "Alte luci", -0.6f, 0.35f, 0.0f, nullptr},
    {"KTV_TrackEV", "Esposizione (EV)", -2.0f, 2.0f, 0.0f, nullptr, "Pista"},
    {"KTV_TrackRecovery", "Alte luci", -0.6f, 0.35f, 0.0f, nullptr},
    {"KTV_FarEV", "Esposizione (EV)", -2.0f, 2.0f, 0.0f, nullptr, "Sfondo e cielo"},
    {"KTV_FarRecovery", "Alte luci", -0.6f, 0.35f, 0.0f, nullptr},
    {"KTV_ShowZones", "Mostra le zone a colori", 0.0f, 1.0f, 0.0f, "No\0Sì\0", "Confini delle zone"},
    {"KTV_DashCenter", "Cruscotto: centro (orizzontale)", 0.2f, 0.8f, 0.5f, nullptr},
    {"KTV_DashWidth", "Cruscotto: larghezza", 0.05f, 0.9f, 0.26f, nullptr},
    {"KTV_DashTop", "Cruscotto: bordo alto (0 = in alto)", 0.0f, 1.0f, 0.5f, nullptr},
    {"KTV_DashBottom", "Cruscotto: bordo basso", 0.0f, 1.0f, 0.85f, nullptr},
    {"KTV_CockpitDistance", "Fine dell'abitacolo (profondità)", 0.5f, 30.0f, 10.0f, nullptr},
    {"KTV_FarDistance", "Inizio dello sfondo (m)", 50.0f, 2000.0f, 400.0f, nullptr},
};
constexpr ParamInfo kBloomParams[] = {
    {"KTV_Threshold", "Soglia", 0.0f, 1.0f, 0.8f, nullptr},
    {"KTV_Knee", "Morbidezza della soglia", 0.01f, 0.5f, 0.2f, nullptr},
};
constexpr ParamInfo kGradeParams[] = {
    {"KTV_Temperature", "Temperatura (freddo - caldo)", -1.0f, 1.0f, 0.0f, nullptr, "Bilanciamento del bianco"},
    {"KTV_Tint", "Tinta (verde - magenta)", -1.0f, 1.0f, 0.0f, nullptr},
    {"KTV_Vibrance", "Vibrance", -1.0f, 1.0f, 0.35f, nullptr, "Colore"},
    {"KTV_Saturation", "Saturazione", 0.0f, 2.0f, 1.05f, nullptr},
    {"KTV_Contrast", "Contrasto (curva a S)", 0.0f, 1.0f, 0.35f, nullptr},
    {"KTV_LutIndex", "LUT", 0.0f, 9.0f, 0.0f, "Neutra\0Broadcast TV\0Pomeriggio caldo\0Nuvoloso freddo\0Cinematica\0Notte\0Stile DLSS5\0Personale 2\0Personale 3\0Personale 4\0", "LUT"},
    {"KTV_LutAmount", "Intensità della LUT", 0.0f, 1.0f, 1.0f, nullptr},
};
constexpr ParamInfo kFogParams[] = {
    {"KTV_Visibility", "Visibilità (m)", 200.0f, 20000.0f, 4000.0f, nullptr},
    {"KTV_SkyAmount", "Foschia sul cielo", 0.0f, 1.0f, 0.3f, nullptr},
    {"KTV_FogTint", "Tinta (fredda - neutra - calda)", -1.0f, 1.0f, 0.0f, nullptr},
    {"KTV_FogBrightness", "Luminosità della foschia", 0.5f, 1.2f, 0.85f, nullptr},
};
constexpr ParamInfo kGIParams[] = {
    {"KTV_GIRadius", "Raggio (m)", 0.3f, 4.0f, 1.5f, nullptr},
    {"KTV_GIShadowsOnly", "Solo nelle zone scure", 0.0f, 1.0f, 0.7f, nullptr},
    {"KTV_GIFovY", "Campo visivo verticale (gradi)", 20.0f, 90.0f, 40.0f, nullptr},
};
constexpr ParamInfo kGtaoParams[] = {
    {"KTV_AORadius", "Raggio (m)", 0.2f, 4.0f, 1.2f, nullptr},
    {"KTV_AOFovY", "Campo visivo verticale (gradi)", 20.0f, 90.0f, 40.0f, nullptr},
    {"KTV_AOMaxDistance", "Distanza massima (m)", 20.0f, 800.0f, 250.0f, nullptr},
};
constexpr ParamInfo kHdrParams[] = {
    {"KTV_PeakNits", "Picco del monitor (nit)", 400.0f, 1600.0f, 1000.0f, nullptr},
    {"KTV_PaperWhite", "Bianco carta (nit)", 80.0f, 400.0f, 200.0f, nullptr},
    {"KTV_Saturation", "Saturazione HDR", 0.8f, 1.3f, 1.0f, nullptr},
};
constexpr ParamInfo kStarGlareParams[] = {
    {"KTV_GlareThreshold", "Soglia (solo luci forti)", 0.5f, 1.0f, 0.85f, nullptr},
    {"KTV_GlareLength", "Lunghezza dei raggi", 0.2f, 2.0f, 1.0f, nullptr},
};
constexpr ParamInfo kSkyParams[] = {
    {"KTV_SkyExposure", "Luminosità del cielo (EV)", -2.0f, 1.0f, -0.5f, nullptr},
    {"KTV_SkySaturation", "Saturazione", 0.0f, 2.0f, 1.2f, nullptr},
    {"KTV_SkyBlue", "Azzurro", -1.0f, 1.0f, 0.2f, nullptr},
    {"KTV_SkyContrast", "Contrasto delle nuvole", 0.0f, 1.0f, 0.3f, nullptr},
    {"KTV_SkyHorizon", "Sfumatura verso l'orizzonte", 0.0f, 1.0f, 0.4f, nullptr},
};
constexpr ParamInfo kRcasParams[] = {
    {"KTV_RcasDenoise", "Riduzione del rumore", 0.0f, 1.0f, 0.5f, nullptr},
};
constexpr ParamInfo kAAParams[] = {
    {"KTV_AAThreshold", "Sensibilità ai bordi", 0.0f, 1.0f, 0.5f, nullptr},
};
constexpr ParamInfo kMotionBlurParams[] = {
    {"KTV_MaxBlur", "Scia massima (% dello schermo)", 0.5f, 8.0f, 3.0f, nullptr},
};
constexpr ParamInfo kDofParams[] = {
    {"KTV_FocusRange", "Profondità del fuoco", 0.05f, 2.0f, 0.4f, nullptr},
    {"KTV_FocusSpeed", "Velocità della messa a fuoco", 0.5f, 10.0f, 3.0f, nullptr},
};
constexpr ParamInfo kLensExtrasParams[] = {
    {"KTV_Aberration", "Aberrazione cromatica", 0.0f, 1.0f, 0.5f, nullptr},
    {"KTV_Grain", "Grana", 0.0f, 1.0f, 0.4f, nullptr},
    {"KTV_Dirt", "Sporco sulla lente", 0.0f, 1.0f, 0.4f, nullptr},
};
constexpr ParamInfo kDebugParams[] = {
    {"KTV_DebugMode", "Vista", 0.0f, 2.0f, 0.0f, "Profondità\0Normali ricostruite\0Metà immagine, metà profondità\0"},
};

// L'ordine di esecuzione degli shader segue il numero nel nome del file
// (ReShade carica i file in ordine alfabetico): FSR prima di tutto, la nebbia
// prima del bloom, il tonemapping dopo il bloom, lo sharpening quasi in fondo.
constexpr std::array kCatalog = {
    // Luce e HDR
    EffectInfo{"tonemap", "Tonemapping ed esposizione", C::LightHdr, "KarmaTV_40_Tonemap.fx", "KarmaTV_Tonemap", 0.0f, 0.1f, 1, true, kTonemapParams},
    EffectInfo{"bloom", "Bloom", C::LightHdr, "KarmaTV_30_Bloom.fx", "KarmaTV_Bloom", 0.3f, 0.6f, 1, true, kBloomParams},
    EffectInfo{"auto_hdr", "AutoHDR (HDR10, picco 1000 nit)", C::LightHdr, "KarmaTV_95_HDR.fx", "KarmaTV_HDR", 0.1f, 0.2f, 1, false, kHdrParams},
    EffectInfo{"star_glare", "Riflessi a stella sui fari", C::LightHdr, "KarmaTV_35_StarGlare.fx", "KarmaTV_StarGlare", 0.3f, 0.5f, 2, true, kStarGlareParams},
    // Colore
    EffectInfo{"grade", "LUT, bilanciamento del bianco, vibrance, curve", C::Color, "KarmaTV_50_Grade.fx", "KarmaTV_Grade", 0.0f, 0.1f, 1, true, kGradeParams},
    EffectInfo{"auto_presets", "Preset automatici per pista e auto", C::Color, nullptr, nullptr, 0.0f, 0.0f, 2, true},
    // Nitidezza, anti-aliasing e scaling
    EffectInfo{"cas", "Sharpening CAS", C::Sharpness, "KarmaTV_60_CAS.fx", "KarmaTV_CAS", 0.1f, 0.2f, 1, true},
    EffectInfo{"af_lod", "Filtro anisotropico e LOD forzati", C::Sharpness, nullptr, nullptr, 0.0f, 0.3f, 2, true},
    EffectInfo{"fsr1", "Nitidezza FSR 1 (RCAS, alternativa al CAS)", C::Sharpness, "KarmaTV_05_FSR1.fx", "KarmaTV_FSR1", 0.05f, 0.1f, 2, true, kRcasParams},
    EffectInfo{"smaa", "Antialiasing post (reti, alberi, linee sottili)", C::Sharpness, "KarmaTV_55_SMAA.fx", "KarmaTV_SMAA", 0.3f, 0.6f, 2, true, kAAParams},
    // Profondita'
    EffectInfo{"gtao", "Occlusione ambientale GTAO", C::Depth, "KarmaTV_20_GTAO.fx", "KarmaTV_GTAO", 0.8f, 2.0f, 1, true, kGtaoParams},
    EffectInfo{"sky", "Cielo (solo il cielo: luminosità, azzurro, nuvole)", C::Depth, "KarmaTV_12_Sky.fx", "KarmaTV_Sky", 0.05f, 0.1f, 2, true, kSkyParams},
    EffectInfo{"fog", "Nebbia e foschia atmosferica", C::Depth, "KarmaTV_10_Fog.fx", "KarmaTV_Fog", 0.1f, 0.2f, 1, true, kFogParams},
    EffectInfo{"dof", "Profondità di campo (replay)", C::Depth, "KarmaTV_65_DOF.fx", "KarmaTV_DOF", 0.5f, 1.5f, 2, false, kDofParams},
    // Ray tracing in screen-space
    EffectInfo{"rtgi", "Luce indiretta (SSGI)", C::ScreenSpaceRT, "KarmaTV_21_RTGI.fx", "KarmaTV_RTGI", 1.0f, 2.5f, 2, false, kGIParams},
    // Camera e lente
    EffectInfo{"lens_extras", "Aberrazione cromatica, grana, sporco lente", C::Camera, "KarmaTV_80_LensExtras.fx", "KarmaTV_LensExtras", 0.0f, 0.1f, 2, false, kLensExtrasParams},
    EffectInfo{"motion_blur", "Motion blur della camera (replay)", C::Camera, "KarmaTV_66_MotionBlur.fx", "KarmaTV_MotionBlur", 0.4f, 1.0f, 2, false, kMotionBlurParams},
    // Interventi sul rendering di rF2
    EffectInfo{"hud_exclusion", "Esclusione dell'HUD dagli effetti", C::Rendering, nullptr, nullptr, 0.0f, 0.0f, 1, true},
    EffectInfo{"debug_views", "Viste di debug (depth, normali)", C::Rendering, "KarmaTV_99_Debug.fx", "KarmaTV_Debug", 0.0f, 0.0f, 1, true, kDebugParams},
    EffectInfo{"mirrors", "Specchietto virtuale con i colori della scena", C::Rendering, nullptr, nullptr, 0.0f, 0.0f, 3, true},
};
}

std::span<const EffectInfo> effect_catalog()
{
    return kCatalog;
}

const EffectInfo *find_effect(std::string_view id)
{
    for (const EffectInfo &effect : kCatalog)
        if (id == effect.id)
            return &effect;
    return nullptr;
}

const char *category_label(Category category)
{
    switch (category)
    {
    case C::LightHdr: return "Luce e HDR";
    case C::Color: return "Colore";
    case C::Sharpness: return "Nitidezza, anti-aliasing e scaling";
    case C::Depth: return "Profondità";
    case C::ScreenSpaceRT: return "Ray tracing in screen-space";
    case C::Camera: return "Camera e lente";
    case C::Rendering: return "Interventi sul rendering di rF2";
    default: return "";
    }
}
}
