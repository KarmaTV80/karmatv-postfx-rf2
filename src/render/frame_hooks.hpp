#pragma once

#include "common.hpp"

// Agganci al frame di rF2 (solo D3D11, solo variante Monitor):
//  - AutoHDR: swapchain convertita in R10G10B10A2 + spazio colore HDR10 (ST.2084),
//    viste del gioco sul back buffer corrette di conseguenza. Vale dal riavvio di rF2.
//  - Esclusione dell'HUD: gli effetti vengono eseguiti al momento del bind del back
//    buffer scelto (N-esimo bind senza depth buffer), prima che rF2 disegni l'HUD.
//  - Analisi di un frame: elenco dei bind di render target nel log, per scegliere N.
namespace ktv::frame
{
struct FrameStats
{
    int backbuffer_binds = 0;          // bind del back buffer (contesto immediato)
    int backbuffer_binds_no_depth = 0; // ... di cui senza depth buffer
    int backbuffer_binds_after_scene = 0; // ... dopo che la scena principale e' stata disegnata
    int draws = 0;                     // draw call sul contesto immediato
    bool hook_fired = false;           // effetti eseguiti prima dell'HUD in questo frame
    bool hook_reached = false;         // il bind scelto per l'aggancio c'e' stato
};

void register_events();
void on_runtime_init(reshade::api::effect_runtime *runtime);
void on_runtime_destroy(reshade::api::effect_runtime *runtime);
void on_runtime_present(reshade::api::effect_runtime *runtime);

void request_analysis();
bool analysis_pending();
const FrameStats &last_frame();

bool mirror_graded();      // specchietto virtuale ripassato con i colori della scena (ultimo frame)
bool hdr_active();         // swapchain davvero in HDR10
bool hdr_restart_needed(); // richiesta HDR diversa da come e' partito rF2
}
