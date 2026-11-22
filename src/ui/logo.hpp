#pragma once

#include "common.hpp"

// Logo KarmaTV: PNG incluso nell'add-on (risorsa 101), decodificato con WIC e caricato
// come texture al primo uso.
namespace ktv::logo
{
// Disegna il logo alto height pixel alla posizione corrente. False se non disponibile.
bool draw(reshade::api::effect_runtime *runtime, float height);
// Larghezza del logo per una data altezza (0 se non caricato).
float width_for(float height);
// Libera la texture (alla chiusura del runtime principale).
void release();
}
