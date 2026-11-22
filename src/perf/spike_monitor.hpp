#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Registro dei picchi: quando un frame dura molto piu' del solito (CPU) o gli effetti
// costano molto piu' del solito (GPU), scrive nel log quanto e' durato, il costo di ogni
// effetto in quel frame e cosa e' cambiato poco prima (effetti accesi o spenti, cambi di
// profilo, specchietti...). Al massimo una riga al secondo, per non causare altri scatti.
namespace ktv::spikes
{
// Evento recente da citare se arriva un picco (resta valido per 1 secondo)
void note(std::string text);
// Una volta per frame, dal runtime principale
void cpu_frame(double now, float dt_seconds);
// Una volta per misura GPU completa: totale e costo di ogni technique (handle, ms)
void gpu_frame(float total_ms, const std::vector<std::pair<uint64_t, float>> &techniques);
// Picchi registrati in questa sessione (per il menu)
int count();
}
