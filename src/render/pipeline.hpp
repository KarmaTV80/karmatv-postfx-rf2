#pragma once

#include "common.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ktv
{
struct App;

// Porta lo stato del profilo dentro ReShade: accende e spegne le technique
// KarmaTV e imposta le uniform (intensita', parametri, confronto A/B).
//
// TODO fase 1:
//  - esclusione dell'HUD: eseguire gli effetti con effect_runtime::render_effects
//    prima della prima draw call 2D di rF2 e saltare il passaggio standard;
//  - AutoHDR: back buffer R10G10B10A2 nell'evento create_swapchain, spazio colore
//    HDR10 (ST.2084) in init_swapchain, codifica PQ nello shader finale.
class EffectPipeline
{
public:
    void apply(reshade::api::effect_runtime *runtime, const App &app);

    // Da chiamare dopo il ricaricamento degli effetti: gli handle non sono piu' validi.
    void invalidate(reshade::api::effect_runtime *runtime);
    void forget(reshade::api::effect_runtime *runtime);

    bool shader_found(reshade::api::effect_runtime *runtime, std::string_view effect_id) const;
    bool is_active(reshade::api::effect_runtime *runtime, std::string_view effect_id) const;
    // Stato letto da ReShade (non quello richiesto): serve alla diagnostica.
    bool reshade_state(reshade::api::effect_runtime *runtime, std::string_view effect_id) const;
    uint64_t technique_handle(reshade::api::effect_runtime *runtime, std::string_view effect_id) const;
    // Replay riconosciuto (scena a schermo intero fuori dall'abitacolo)
    bool replay_detected() const { return replay_detected_; }

private:
    struct Cached
    {
        reshade::api::effect_technique technique = {0};
        reshade::api::effect_uniform_variable intensity = {0};
        reshade::api::effect_uniform_variable split = {0};
        reshade::api::effect_uniform_variable depth_reversed = {0};
        reshade::api::effect_uniform_variable depth_far = {0};
        reshade::api::effect_uniform_variable depth_valid = {0};
        reshade::api::effect_uniform_variable camera_cockpit = {0};
        std::vector<reshade::api::effect_uniform_variable> params;
        bool resolved = false;
        bool active = false;
        bool reshade_on = false;
        bool forced_on = false; // acceso da un automatismo (con isteresi)
        int state_changes = 0; // per limitare i messaggi nel log
    };
    using EffectMap = std::unordered_map<std::string, Cached>;

    const Cached *lookup(reshade::api::effect_runtime *runtime, std::string_view effect_id) const;

    std::unordered_map<reshade::api::effect_runtime *, EffectMap> cache_;
    bool replay_detected_ = false;
};
}
