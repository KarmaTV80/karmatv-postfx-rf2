#pragma once

#include "common.hpp"

namespace ktv
{
// Finestra KarmaTV PostFX, indipendente dall'overlay di ReShade.
// Si apre e si chiude con Bloc Scorr; mentre e' aperta l'input al gioco e' bloccato.
void draw_main_window(reshade::api::effect_runtime *runtime);

// Avviso breve nell'angolo (cambio profilo, salvataggio), visibile anche a finestra chiusa.
void draw_toast(reshade::api::effect_runtime *runtime);
}
