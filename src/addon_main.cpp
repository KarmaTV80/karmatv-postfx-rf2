// KarmaTV PostFX - add-on ReShade per rFactor 2.
// Punto d'ingresso: registrazione presso ReShade ed eventi per frame.

#include "common.hpp"
#include "app.hpp"
#include "ui/logo.hpp"
#include "core/log.hpp"
#include "input/wheel.hpp"
#include "render/depth_capture.hpp"
#include "render/frame_hooks.hpp"
#include "ui/overlay.hpp"

#include <d3d11.h>

extern "C" __declspec(dllexport) const char *NAME = "KarmaTV PostFX";
extern "C" __declspec(dllexport) const char *DESCRIPTION =
    "Effetti grafici per rFactor 2 con profili Monitor/VR, automatismi da telemetria e menu in gioco.";

namespace
{
using namespace reshade::api;

bool is_d3d11(effect_runtime *runtime)
{
    return runtime->get_device()->get_api() == device_api::d3d11;
}

ID3D11DeviceContext *native_context(command_list *cmd_list)
{
    return reinterpret_cast<ID3D11DeviceContext *>(cmd_list->get_native());
}

void on_init_effect_runtime(effect_runtime *runtime)
{
    ktv::App &a = ktv::app();

    // Inizializzazione (cartelle, preset) fuori da DllMain, dove l'I/O su file e' sconsigliato.
    a.ensure_initialized();

    uint32_t width = 0, height = 0;
    runtime->get_screenshot_width_and_height(&width, &height);
    ktv::log::info("Runtime ReShade avviato: " + std::to_string(width) + "x" + std::to_string(height) +
                   (a.primary_runtime == nullptr ? " (principale)" : " (secondario)"));

    if (a.primary_runtime == nullptr)
        a.primary_runtime = runtime;
    ktv::frame::on_runtime_init(runtime);
    ktv::depth::on_runtime_init(runtime);

    if (is_d3d11(runtime))
        a.profiler.init(reinterpret_cast<ID3D11Device *>(runtime->get_device()->get_native()));
    else
        ktv::log::warning("API grafica diversa da D3D11: profiler GPU disattivato.");
}

void on_destroy_effect_runtime(effect_runtime *runtime)
{
    ktv::App &a = ktv::app();
    a.pipeline.forget(runtime);
    ktv::frame::on_runtime_destroy(runtime);
    ktv::depth::on_runtime_destroy(runtime);
    if (a.primary_runtime == runtime)
    {
        ktv::wheel::shutdown();
        ktv::stop_vr_watch();
        a.primary_runtime = nullptr;
        a.profiler.release();
        ktv::logo::release();
    }
}

void on_reloaded_effects(effect_runtime *runtime)
{
    ktv::app().pipeline.invalidate(runtime);
    ktv::depth::on_reloaded_effects(runtime);
}

void on_begin_effects(effect_runtime *runtime, command_list *cmd_list, resource_view, resource_view)
{
    ktv::depth::on_begin_effects(runtime, cmd_list);
    if (is_d3d11(runtime))
        ktv::app().profiler.begin(native_context(cmd_list));
}

void on_render_technique(effect_runtime *runtime, effect_technique technique, command_list *cmd_list, resource_view, resource_view)
{
    if (is_d3d11(runtime))
        ktv::app().profiler.mark_technique(native_context(cmd_list), technique.handle);
}

void on_finish_effects(effect_runtime *runtime, command_list *cmd_list, resource_view, resource_view)
{
    if (is_d3d11(runtime))
        ktv::app().profiler.end(native_context(cmd_list));
}

void on_present(effect_runtime *runtime)
{
    ktv::App &a = ktv::app();
    if (runtime == a.primary_runtime)
    {
        a.update_frame(runtime);

        if (is_d3d11(runtime) && a.profiler.available())
        {
            auto *device = reinterpret_cast<ID3D11Device *>(runtime->get_device()->get_native());
            ID3D11DeviceContext *context = nullptr;
            device->GetImmediateContext(&context);
            if (context != nullptr)
            {
                a.profiler.collect(context);
                context->Release();
            }
        }
    }

    // Lo stato vale per il frame successivo (anche per il runtime del visore in VR).
    a.pipeline.apply(runtime, a);
    ktv::frame::on_runtime_present(runtime);
    ktv::depth::on_runtime_present(runtime);
}

// Chiamato ad ogni frame (anche a overlay di ReShade chiuso): finestra KarmaTV e avvisi.
void on_overlay(effect_runtime *runtime)
{
    ktv::draw_main_window(runtime);
    ktv::draw_toast(runtime);
}
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        if (!reshade::register_addon(module))
            return FALSE;

        ktv::app().module = module;

        ktv::frame::register_events();
        ktv::depth::register_events();
        reshade::register_event<reshade::addon_event::init_effect_runtime>(&on_init_effect_runtime);
        reshade::register_event<reshade::addon_event::destroy_effect_runtime>(&on_destroy_effect_runtime);
        reshade::register_event<reshade::addon_event::reshade_reloaded_effects>(&on_reloaded_effects);
        reshade::register_event<reshade::addon_event::reshade_begin_effects>(&on_begin_effects);
        reshade::register_event<reshade::addon_event::reshade_render_technique>(&on_render_technique);
        reshade::register_event<reshade::addon_event::reshade_finish_effects>(&on_finish_effects);
        reshade::register_event<reshade::addon_event::reshade_present>(&on_present);
        reshade::register_event<reshade::addon_event::reshade_overlay>(&on_overlay);
        break;

    case DLL_PROCESS_DETACH:
        // Eventi e overlay vengono rimossi da ReShade insieme all'add-on.
        reshade::unregister_addon(module);
        break;
    }
    return TRUE;
}
