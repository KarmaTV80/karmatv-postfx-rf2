#pragma once

#include <span>
#include <string_view>

namespace ktv
{
// Categorie come nella specifica (sezione "Catalogo effetti").
enum class Category
{
    LightHdr,
    Color,
    Sharpness,
    Depth,
    ScreenSpaceRT,
    Camera,
    Rendering,
    Count
};

// Parametro aggiuntivo di uno shader, regolabile dalla finestra KarmaTV.
struct ParamInfo
{
    const char *uniform; // nome della uniform nel file .fx
    const char *label;   // nome nel menu
    float min_value;
    float max_value;
    float default_value;
    const char *items;   // nullptr = cursore; altrimenti voci di una lista separate da '\0' (uniform int)
    const char *group = nullptr; // titolo di un nuovo gruppo che inizia con questo parametro
};

struct EffectInfo
{
    const char *id;        // chiave stabile, usata nei preset .ini
    const char *label;     // nome mostrato nel menu
    Category category;
    const char *fx_file;   // file shader ReShade; nullptr = funzione gestita dall'add-on
    const char *technique; // nome della technique dentro il file .fx
    float cost_min_ms;     // stima a 5120x1440 su RTX 5080
    float cost_max_ms;
    int phase;             // fase di sviluppo (1, 2 o 3)
    bool vr_allowed;       // consentito nella variante VR
    std::span<const ParamInfo> params = {};
};

std::span<const EffectInfo> effect_catalog();
const EffectInfo *find_effect(std::string_view id);
const char *category_label(Category category);
}
