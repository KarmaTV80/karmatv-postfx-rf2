#include "render/pipeline.hpp"
#include "app.hpp"
#include "core/effects_catalog.hpp"
#include "core/log.hpp"
#include "render/depth_capture.hpp"
#include "render/frame_hooks.hpp"
#include "perf/spike_monitor.hpp"

#include <algorithm>
#include <cmath>

namespace ktv
{
void EffectPipeline::apply(reshade::api::effect_runtime *runtime, const App &app)
{
    EffectMap &effects = cache_[runtime];
    const bool vr = app.active_variant() == Variant::VR;
    const bool rules_active = app.profile.features.rules_enabled;
    const float split = app.split_compare ? 0.5f : 0.0f;

    // Profondita': invertita secondo il valore con cui rF2 cancella il buffer, salvo scelta manuale.
    const int depth_mode = app.profile.features.depth_mode;
    const depth::Status &depth_status = depth::status();
    const bool depth_reversed = depth_mode == 2 || (depth_mode == 0 && depth_status.reversed_known && depth_status.reversed);
    const float depth_far = app.profile.features.depth_far;
    // Nei menu rF2 disegna comunque la scena (per la finestrella "Watch") a schermo intero:
    // la profondita' e' aggiornata ma non corrisponde all'immagine. Gli effetti di
    // profondita' valgono quindi solo al volante in pista (telemetria mInRealtime).
    const TelemetrySnapshot &telemetry = app.telemetry.snapshot();
    // Con il profilo Replay attivo la profondita' vale anche fuori dall'abitacolo (replay a
    // schermo intero, dove serve la profondita' di campo): il profilo si sceglie a mano.
    // Replay: telemetria fuori dall'abitacolo ma scena 3D disegnata in tutti i frame per
    // almeno ~1 secondo (90 frame). Nei menu la finestrella "Watch" non arriva a tanto.
    // Una volta riconosciuto, il replay resta tale anche se qualche frame arriva senza
    // profondita' (cambi di telecamera, caricamenti dei settori): si esce solo quando ne
    // manca piu' di 1 su 4 negli ultimi 64 frame (menu, finestrella "Watch" a frame alterni).
    static bool replay = false;
    static uint64_t fresh_history = 0;
    const bool replay_candidate = telemetry.connected && !telemetry.in_realtime && app.profile.features.replay_effects;
    const uint32_t streak = depth::fresh_streak();
    fresh_history = (fresh_history << 1) | (depth::depth_fresh() ? 1u : 0u);
    int misses = 0;
    for (uint64_t bits = ~fresh_history; bits != 0; bits &= bits - 1)
        ++misses;
    const bool was_replay = replay;
    if (!replay_candidate)
        replay = false;
    else if (!replay && streak >= 90)
        replay = true;
    else if (replay && misses >= 24)
        replay = false;
    if (replay != was_replay)
    {
        // Ultimi 64 frame (# = scena 3D disegnata, . = assente): serve a capire lo schema
        std::string pattern;
        for (int i = 63; i >= 0; --i)
            pattern += ((fresh_history >> i) & 1u) ? '#' : '.';
        log::info(replay ? "Replay rilevato: effetti attivi." :
                  (replay_candidate ? "Fine replay (scena 3D assente in " + std::to_string(misses) + " frame su 64): effetti in pausa. " + pattern :
                                      std::string("Fine replay (di nuovo al volante o telemetria persa).")));
    }
    replay_detected_ = replay;
    const bool driving = !telemetry.connected || telemetry.in_realtime || app.profile.name == "Replay Karma FX" || replay;
    // Nel replay un frame senza profondita' nuova usa l'ultima copiata, invece di spegnere
    // per un frame gli effetti di profondita' (nebbia, DOF, GTAO lampeggerebbero)
    const bool depth_valid = (depth::depth_fresh() || replay) && driving;

    // Dissolvenza: entrando in pista o nel replay gli effetti salgono in ~0.5 s, tornando
    // ai menu scendono in ~0.35 s. Anche un singolo frame "fuori scena" non li fa sparire.
    static double fade_last = 0.0;
    static float fade = 0.0f;
    const double fade_now = now_seconds();
    const float fade_dt = fade_last > 0.0 ? static_cast<float>(std::min(fade_now - fade_last, 0.1)) : 0.0f;
    fade_last = fade_now;
    if (driving)
        fade = std::min(1.0f, fade + fade_dt / 0.5f);
    else
        fade = std::max(0.0f, fade - fade_dt / 0.35f);
    const bool scene_on = driving || fade > 0.0f;

    for (const EffectInfo &info : effect_catalog())
    {
        if (info.fx_file == nullptr)
            continue;

        Cached &c = effects[info.id];
        if (!c.resolved)
        {
            c.technique = runtime->find_technique(info.fx_file, info.technique);
            c.intensity = runtime->find_uniform_variable(info.fx_file, "KTV_Intensity");
            c.split = runtime->find_uniform_variable(info.fx_file, "KTV_SplitX");
            c.depth_reversed = runtime->find_uniform_variable(info.fx_file, "KTV_DepthReversed");
            c.depth_far = runtime->find_uniform_variable(info.fx_file, "KTV_DepthFar");
            c.depth_valid = runtime->find_uniform_variable(info.fx_file, "KTV_DepthValid");
            c.camera_cockpit = runtime->find_uniform_variable(info.fx_file, "KTV_CameraCockpit");
            c.params.clear();
            for (const ParamInfo &param : info.params)
                c.params.push_back(runtime->find_uniform_variable(info.fx_file, param.uniform));
            c.resolved = true;
            if (c.technique.handle != 0)
                log::info(std::string("Shader trovato: ") + info.fx_file);
        }
        if (c.technique.handle == 0)
        {
            c.active = false;
            continue; // shader non ancora scritto o non installato
        }

        const EffectSetting setting = effect_setting(app.profile, info.id);
        const EffectModifier mod = rules_active ? app.rules.modifier(info.id) : EffectModifier{};

        // Effetto acceso da un automatismo: si accende sopra 0.05 e si spegne sotto 0.01,
        // cosi' un valore che oscilla vicino alla soglia non lo fa lampeggiare
        c.forced_on = c.forced_on ? mod.force_weight > 0.01f : mod.force_weight > 0.05f;
        const bool wanted = setting.enabled || c.forced_on;
        // Nei menu gli effetti si fermano: rF2 disegna la scena della finestrella "Watch"
        // solo in alcuni frame, e gli effetti finirebbero sul menu a frame alterni (sfarfallio).
        // "Originale rF2": nessun effetto, nemmeno quelli che accenderebbero gli automatismi
        const bool original = app.profile.name == "Originale rF2";
        bool on = app.master_enabled && !original && wanted && (!vr || info.vr_allowed) && scene_on;
        // La codifica HDR10 la fa l'add-on a fine frame (frame_hooks.cpp), dopo HUD e
        // specchietto: lo shader ReShade equivalente resta sempre spento.
        if (std::string_view(info.id) == "auto_hdr")
            on = false;

        float intensity = setting.intensity * mod.multiplier;
        if (!setting.enabled)
            intensity *= mod.force_weight; // acceso solo dalla regola: entra in dissolvenza

        if (runtime->get_technique_state(c.technique) != on)
        {
            runtime->set_technique_state(c.technique, on);
            spikes::note(std::string(info.technique) + (on ? " acceso" : " spento"));
            if (c.state_changes < 20)
            {
                ++c.state_changes;
                const bool now_on = runtime->get_technique_state(c.technique);
                log::info(std::string(info.technique) + (on ? " attivato" : " disattivato") +
                          (now_on == on ? "" : " (ReShade NON ha applicato la modifica)"));
            }
        }
        c.reshade_on = runtime->get_technique_state(c.technique);
        c.active = on;

        if (!on)
            continue;
        if (c.intensity.handle != 0)
        {
            const float value = intensity;
            runtime->set_uniform_value_float(c.intensity, &value, 1);
        }
        if (c.depth_reversed.handle != 0)
        {
            const bool value = depth_reversed;
            runtime->set_uniform_value_bool(c.depth_reversed, &value, 1);
        }
        if (c.depth_far.handle != 0)
            runtime->set_uniform_value_float(c.depth_far, &depth_far, 1);
        if (c.depth_valid.handle != 0)
        {
            const bool value = depth_valid;
            runtime->set_uniform_value_bool(c.depth_valid, &value, 1);
        }
        if (c.camera_cockpit.handle != 0)
        {
            // 1 = visuale abitacolo (anche quella "TV"), 0 = altra telecamera, -1 = non nota
            const int type = telemetry.camera_type;
            const int32_t value = type < 0 ? -1 : (type == 0 || type == 1 ? 1 : 0);
            runtime->set_uniform_value_int(c.camera_cockpit, &value, 1);
        }
        if (c.split.handle != 0)
        {
            const float split_value = std::string_view(info.id) == "auto_hdr" ? 0.0f : split;
            runtime->set_uniform_value_float(c.split, &split_value, 1);
        }

        for (size_t i = 0; i < c.params.size() && i < info.params.size(); ++i)
        {
            if (c.params[i].handle == 0)
                continue;
            const ParamInfo &param = info.params[i];
            const float value = param_value(setting, param);
            if (param.items != nullptr)
            {
                const int32_t index = static_cast<int32_t>(std::lround(value));
                runtime->set_uniform_value_int(c.params[i], &index, 1);
            }
            else
            {
                runtime->set_uniform_value_float(c.params[i], &value, 1);
            }
        }
    }

    // Passaggi della dissolvenza: accesi solo mentre l'immagine sta cambiando
    Cached &save = effects["__fade_save"];
    Cached &mix = effects["__fade_mix"];
    if (!save.resolved)
    {
        save.technique = runtime->find_technique("KarmaTV_01_Fade.fx", "KarmaTV_FadeSave");
        mix.technique = runtime->find_technique("KarmaTV_01_Fade.fx", "KarmaTV_FadeMix");
        mix.intensity = runtime->find_uniform_variable("KarmaTV_01_Fade.fx", "KTV_Fade");
        save.resolved = mix.resolved = true;
    }
    const bool fading = app.master_enabled && app.profile.name != "Originale rF2" && fade > 0.0f && fade < 1.0f;
    if (save.technique.handle != 0 && mix.technique.handle != 0)
    {
        if (runtime->get_technique_state(save.technique) != fading)
            runtime->set_technique_state(save.technique, fading);
        if (runtime->get_technique_state(mix.technique) != fading)
            runtime->set_technique_state(mix.technique, fading);
        if (fading && mix.intensity.handle != 0)
            runtime->set_uniform_value_float(mix.intensity, &fade, 1);
    }
}

void EffectPipeline::invalidate(reshade::api::effect_runtime *runtime)
{
    cache_[runtime].clear();
}

void EffectPipeline::forget(reshade::api::effect_runtime *runtime)
{
    cache_.erase(runtime);
}

const EffectPipeline::Cached *EffectPipeline::lookup(reshade::api::effect_runtime *runtime, std::string_view effect_id) const
{
    const auto rt = cache_.find(runtime);
    if (rt == cache_.end())
        return nullptr;
    const auto it = rt->second.find(std::string(effect_id));
    return it != rt->second.end() ? &it->second : nullptr;
}

bool EffectPipeline::shader_found(reshade::api::effect_runtime *runtime, std::string_view effect_id) const
{
    const Cached *c = lookup(runtime, effect_id);
    return c != nullptr && c->technique.handle != 0;
}

bool EffectPipeline::is_active(reshade::api::effect_runtime *runtime, std::string_view effect_id) const
{
    const Cached *c = lookup(runtime, effect_id);
    return c != nullptr && c->active;
}

bool EffectPipeline::reshade_state(reshade::api::effect_runtime *runtime, std::string_view effect_id) const
{
    const Cached *c = lookup(runtime, effect_id);
    return c != nullptr && c->reshade_on;
}

uint64_t EffectPipeline::technique_handle(reshade::api::effect_runtime *runtime, std::string_view effect_id) const
{
    const Cached *c = lookup(runtime, effect_id);
    return c != nullptr ? c->technique.handle : 0;
}
}
