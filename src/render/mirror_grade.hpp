#pragma once

#include <d3d11.h>

// Specchietto virtuale con gli stessi colori della scena (fase 3).
//
// rF2 disegna gli specchietti in una texture a parte (2048x512 con MSAA, in HDR) e ne
// prepara la versione finale RGBA8 dopo il punto in cui KarmaTV applica gli effetti:
// lo specchietto virtuale dell'HUD resterebbe quindi con i colori originali. Qui la texture
// finale viene ripassata con lo stesso tonemapping (esposizione automatica compresa, letta
// dallo shader Tonemap) e la stessa correzione colore e LUT dello shader Grade.
// Niente effetti di profondita': la profondita' della scena non vale per lo specchietto.
namespace ktv::mirror
{
struct GradeParams
{
    // Tonemapping (intensita' 0 = spento)
    float tonemap_intensity = 0.0f;
    float tonemap_mode = 0.0f;
    float exposure_ev = 0.0f;
    float hdr_recovery = 0.6f;
    float auto_exposure = 0.0f;
    // Grade (intensita' 0 = spento)
    float grade_intensity = 0.0f;
    float temperature = 0.0f;
    float tint = 0.0f;
    float vibrance = 0.0f;
    float saturation = 1.0f;
    float contrast = 0.0f;
    float lut_index = 0.0f;
    float lut_amount = 1.0f;
};

// Applica il passaggio sulla texture (sul posto). lut e adapt possono essere nullptr.
bool apply(ID3D11DeviceContext *context, ID3D11Texture2D *texture, const GradeParams &params,
           ID3D11ShaderResourceView *lut, ID3D11ShaderResourceView *adapt);

void release();
}
