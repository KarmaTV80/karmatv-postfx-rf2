#pragma once

#include "common.hpp"

#include <cstdint>

// Profondita' di rF2 per gli shader KarmaTV, anche con MSAA attivo.
//
// In D3D11 ReShade non sa leggere un depth buffer multicampionato (non si puo'
// "risolvere" la profondita'), quindi il suo add-on Generic Depth lo scarta.
// Qui invece:
//  1. alla creazione, il depth buffer MSAA di rF2 viene reso leggibile dagli shader
//     (formato "typeless" + uso shader_resource);
//  2. ogni frame viene scelto quello grande come lo schermo con piu' draw call;
//  3. prima che rF2 lo cancelli (o prima degli effetti) un piccolo shader ne copia il
//     campione 0 in una texture R32_FLOAT normale;
//  4. la texture viene collegata agli shader con la semantica KARMATV_DEPTH.
// Dal valore con cui rF2 cancella il buffer si capisce se la profondita' e' invertita.
namespace ktv::depth
{
struct Status
{
    bool found = false;        // depth buffer scelto
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t samples = 0;
    uint32_t format = 0;       // formato DXGI originale
    bool reversed_known = false;
    bool reversed = false;     // rF2 cancella a 0 = profondita' invertita (reverse-Z)
    uint64_t copies = 0;       // copie eseguite
    const char *error = nullptr;
};

void register_events();
void on_runtime_init(reshade::api::effect_runtime *runtime);
void on_runtime_destroy(reshade::api::effect_runtime *runtime);
void on_reloaded_effects(reshade::api::effect_runtime *runtime);
void on_begin_effects(reshade::api::effect_runtime *runtime, reshade::api::command_list *cmd_list);
void on_runtime_present(reshade::api::effect_runtime *runtime);

const Status &status();

// true quando nel frame corrente la scena principale e' gia' stata disegnata
// (il depth buffer scelto ha ricevuto almeno il 90% delle draw del frame precedente).
bool scene_drawn_this_frame();

// true se in questo frame rF2 ha disegnato la scena 3D (profondita' gia' copiata o draw
// sul depth buffer scelto). Nei menu con la finestrella "Watch" e' false a frame alterni.
bool scene_in_this_frame();

// true se nel frame appena concluso la profondita' e' stata copiata (scena in pista).
// Nei menu e nei caricamenti e' false: gli effetti di profondita' si spengono.
bool depth_fresh();


// Frame consecutivi con profondita' copiata. Nei replay la scena arriva ogni frame;
// nella finestrella "Watch" dei menu solo a frame alterni, quindi la serie si interrompe.
uint32_t fresh_streak();
}
