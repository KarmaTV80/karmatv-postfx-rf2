#include "render/depth_capture.hpp"
#include "app.hpp"
#include "core/log.hpp"

#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <mutex>
#include <string>
#include <unordered_map>

namespace ktv::depth
{
namespace
{
using namespace reshade::api;
using Microsoft::WRL::ComPtr;

constexpr const char *kSemantic = "KARMATV_DEPTH";

struct Candidate
{
    resource_desc desc;
    uint32_t draws_this_frame = 0;
    uint32_t draws_since_clear = 0;
    uint32_t best_segment_last_frame = 0; // draw del passaggio principale nel frame precedente
    uint32_t best_segment_this_frame = 0; // ... e in quello in corso
};

std::mutex g_mutex;
std::unordered_map<uint64_t, Candidate> g_candidates;          // depth buffer MSAA di rF2
std::unordered_map<command_list *, uint64_t> g_bound_depth;   // depth legato per ogni command list
uint64_t g_selected = 0;
uint32_t g_scene_draws_last_frame = 0;
bool g_copied_this_frame = false;
bool g_last_frame_fresh = false;
uint32_t g_fresh_streak = 0;
uint32_t g_screen_width = 0, g_screen_height = 0;

Status g_status;

// Risorse D3D11 nostre
ID3D11Device *g_d3d = nullptr;
ComPtr<ID3D11VertexShader> g_vs;
ComPtr<ID3D11PixelShader> g_ps;
ComPtr<ID3DDeviceContextState> g_state;
ComPtr<ID3D11Texture2D> g_copy_texture;
ComPtr<ID3D11RenderTargetView> g_copy_rtv;
ComPtr<ID3D11ShaderResourceView> g_copy_srv;
ComPtr<ID3D11ShaderResourceView> g_source_srv; // vista multicampione del depth di rF2
uint64_t g_source_for = 0;                      // risorsa a cui si riferisce g_source_srv
bool g_shaders_failed = false;

format to_typeless(format f)
{
    switch (f)
    {
    case format::d24_unorm_s8_uint: return format::r24_g8_typeless;
    case format::d32_float: return format::r32_typeless;
    case format::d32_float_s8_uint: return format::r32_g8_typeless;
    case format::d16_unorm: return format::r16_typeless;
    default: return format::unknown;
    }
}

DXGI_FORMAT to_readable(format typeless)
{
    switch (typeless)
    {
    case format::r24_g8_typeless: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case format::r32_typeless: return DXGI_FORMAT_R32_FLOAT;
    case format::r32_g8_typeless: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    case format::r16_typeless: return DXGI_FORMAT_R16_UNORM;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}

// --- Shader di copia (compilati una volta con d3dcompiler_47) -----------------------

constexpr const char kShaderSource[] = R"(
Texture2DMS<float> depth_ms : register(t0);
void vs_main(uint id : SV_VertexID, out float4 position : SV_Position)
{
    float2 uv = float2((id << 1) & 2, id & 2);
    position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
float ps_main(float4 position : SV_Position) : SV_Target
{
    return depth_ms.Load(int2(position.xy), 0);
}
)";

bool create_shaders()
{
    if (g_vs != nullptr)
        return true;
    if (g_shaders_failed || g_d3d == nullptr)
        return false;

    using D3DCompileFn = HRESULT(WINAPI *)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO *, ID3DInclude *, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);
    HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
    const auto compile = compiler != nullptr ? reinterpret_cast<D3DCompileFn>(GetProcAddress(compiler, "D3DCompile")) : nullptr;
    if (compile == nullptr)
    {
        g_shaders_failed = true;
        g_status.error = "d3dcompiler_47.dll non disponibile";
        log::error("Profondità: d3dcompiler_47.dll non disponibile.");
        return false;
    }

    ComPtr<ID3DBlob> vs_code, ps_code, errors;
    const bool ok =
        SUCCEEDED(compile(kShaderSource, sizeof(kShaderSource) - 1, "karmatv_depth", nullptr, nullptr, "vs_main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vs_code, &errors)) &&
        SUCCEEDED(compile(kShaderSource, sizeof(kShaderSource) - 1, "karmatv_depth", nullptr, nullptr, "ps_main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps_code, &errors)) &&
        SUCCEEDED(g_d3d->CreateVertexShader(vs_code->GetBufferPointer(), vs_code->GetBufferSize(), nullptr, &g_vs)) &&
        SUCCEEDED(g_d3d->CreatePixelShader(ps_code->GetBufferPointer(), ps_code->GetBufferSize(), nullptr, &g_ps));
    if (!ok)
    {
        g_shaders_failed = true;
        g_status.error = "compilazione dello shader di copia fallita";
        log::error(std::string("Profondità: compilazione dello shader di copia fallita. ") +
                   (errors != nullptr ? static_cast<const char *>(errors->GetBufferPointer()) : ""));
        g_vs.Reset();
        g_ps.Reset();
        return false;
    }

    ComPtr<ID3D11Device1> d3d1;
    if (SUCCEEDED(g_d3d->QueryInterface(IID_PPV_ARGS(&d3d1))))
    {
        const D3D_FEATURE_LEVEL level = g_d3d->GetFeatureLevel();
        d3d1->CreateDeviceContextState(0, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1), nullptr, &g_state);
    }
    if (g_state == nullptr)
    {
        g_shaders_failed = true;
        g_status.error = "stato D3D11 di appoggio non disponibile";
        return false;
    }
    return true;
}

// Texture di destinazione grande come il depth buffer, e vista multicampione su di esso.
bool prepare_targets(uint64_t source, const Candidate &candidate)
{
    const uint32_t width = candidate.desc.texture.width;
    const uint32_t height = candidate.desc.texture.height;

    D3D11_TEXTURE2D_DESC current = {};
    if (g_copy_texture != nullptr)
        g_copy_texture->GetDesc(&current);
    if (g_copy_texture == nullptr || current.Width != width || current.Height != height)
    {
        g_copy_texture.Reset();
        g_copy_rtv.Reset();
        g_copy_srv.Reset();
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(g_d3d->CreateTexture2D(&desc, nullptr, &g_copy_texture)) ||
            FAILED(g_d3d->CreateRenderTargetView(g_copy_texture.Get(), nullptr, &g_copy_rtv)) ||
            FAILED(g_d3d->CreateShaderResourceView(g_copy_texture.Get(), nullptr, &g_copy_srv)))
        {
            g_status.error = "creazione della texture di profondità fallita";
            return false;
        }
    }

    if (g_source_for != source)
    {
        g_source_srv.Reset();
        D3D11_SHADER_RESOURCE_VIEW_DESC view = {};
        view.Format = to_readable(candidate.desc.texture.format);
        view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
        if (view.Format == DXGI_FORMAT_UNKNOWN ||
            FAILED(g_d3d->CreateShaderResourceView(reinterpret_cast<ID3D11Resource *>(source), &view, &g_source_srv)))
        {
            g_status.error = "il depth buffer di rF2 non è leggibile";
            return false;
        }
        g_source_for = source;
    }
    return true;
}

void copy_now(command_list *cmd_list)
{
    if (g_selected == 0 || !create_shaders())
        return;
    const auto it = g_candidates.find(g_selected);
    if (it == g_candidates.end() || !prepare_targets(g_selected, it->second))
        return;

    auto *context = reinterpret_cast<ID3D11DeviceContext *>(cmd_list->get_native());
    ComPtr<ID3D11DeviceContext1> context1;
    if (FAILED(context->QueryInterface(IID_PPV_ARGS(&context1))))
        return;

    // Stato di rF2 messo da parte e ripristinato identico.
    ID3DDeviceContextState *previous = nullptr;
    context1->SwapDeviceContextState(g_state.Get(), &previous);

    const D3D11_VIEWPORT viewport = {0.0f, 0.0f, static_cast<float>(it->second.desc.texture.width), static_cast<float>(it->second.desc.texture.height), 0.0f, 1.0f};
    ID3D11RenderTargetView *rtv = g_copy_rtv.Get();
    ID3D11ShaderResourceView *srv = g_source_srv.Get();
    context1->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context1->IASetInputLayout(nullptr);
    context1->VSSetShader(g_vs.Get(), nullptr, 0);
    context1->PSSetShader(g_ps.Get(), nullptr, 0);
    context1->OMSetRenderTargets(1, &rtv, nullptr);
    context1->RSSetViewports(1, &viewport);
    context1->PSSetShaderResources(0, 1, &srv);
    context1->Draw(3, 0);
    ID3D11ShaderResourceView *none = nullptr;
    context1->PSSetShaderResources(0, 1, &none);

    context1->SwapDeviceContextState(previous, nullptr);
    if (previous != nullptr)
        previous->Release();

    g_copied_this_frame = true;
    ++g_status.copies;
}

void bind_to_effects(effect_runtime *runtime)
{
    const resource_view view = {reinterpret_cast<uint64_t>(g_copy_srv.Get())};
    runtime->update_texture_bindings(kSemantic, view, view);
}

// --- Eventi -------------------------------------------------------------------------

bool on_create_resource(device *dev, resource_desc &desc, subresource_data *, resource_usage)
{
    if (dev->get_api() != device_api::d3d11 || desc.type != resource_type::texture_2d || desc.texture.samples <= 1 ||
        (desc.usage & resource_usage::depth_stencil) == 0)
        return false;
    const format typeless = to_typeless(desc.texture.format);
    if (typeless == format::unknown)
        return false;
    desc.texture.format = typeless;
    desc.usage |= resource_usage::shader_resource;
    return true;
}

void on_init_resource(device *dev, const resource_desc &desc, const subresource_data *, resource_usage, resource r)
{
    if (dev->get_api() != device_api::d3d11 || desc.type != resource_type::texture_2d || desc.texture.samples <= 1 ||
        (desc.usage & resource_usage::depth_stencil) == 0 || to_readable(desc.texture.format) == DXGI_FORMAT_UNKNOWN)
        return;
    const std::lock_guard<std::mutex> lock(g_mutex);
    Candidate candidate;
    candidate.desc = desc;
    g_candidates[r.handle] = candidate;
}

void on_destroy_resource(device *, resource r)
{
    const std::lock_guard<std::mutex> lock(g_mutex);
    if (g_candidates.erase(r.handle) == 0)
        return;
    if (g_source_for == r.handle)
    {
        g_source_srv.Reset();
        g_source_for = 0;
    }
    if (g_selected == r.handle)
        g_selected = 0;
}

void on_bind(command_list *cmd_list, uint32_t, const resource_view *, resource_view dsv)
{
    uint64_t target = 0;
    if (dsv.handle != 0)
        target = cmd_list->get_device()->get_resource_from_view(dsv).handle;
    const std::lock_guard<std::mutex> lock(g_mutex);
    g_bound_depth[cmd_list] = target;
}

void count_draw(command_list *cmd_list)
{
    const std::lock_guard<std::mutex> lock(g_mutex);
    const auto bound = g_bound_depth.find(cmd_list);
    if (bound == g_bound_depth.end() || bound->second == 0)
        return;
    const auto it = g_candidates.find(bound->second);
    if (it == g_candidates.end())
        return;
    ++it->second.draws_this_frame;
    ++it->second.draws_since_clear;
}

bool on_draw(command_list *cmd_list, uint32_t, uint32_t, uint32_t, uint32_t)
{
    count_draw(cmd_list);
    return false;
}

bool on_draw_indexed(command_list *cmd_list, uint32_t, uint32_t, uint32_t, int32_t, uint32_t)
{
    count_draw(cmd_list);
    return false;
}

bool on_clear_depth(command_list *cmd_list, resource_view dsv, const float *depth, const uint8_t *, uint32_t, const rect *)
{
    const uint64_t target = cmd_list->get_device()->get_resource_from_view(dsv).handle;
    const std::lock_guard<std::mutex> lock(g_mutex);
    const auto it = g_candidates.find(target);
    if (it == g_candidates.end())
        return false;

    if (target == g_selected && depth != nullptr)
    {
        g_status.reversed_known = true;
        g_status.reversed = *depth < 0.5f;
    }

    // Copia prima della cancellazione se il passaggio appena finito e' quello principale
    // (almeno meta' delle draw del passaggio piu' grande del frame precedente).
    const App &a = app();
    const bool main_pass = it->second.draws_since_clear > 0 &&
                           it->second.draws_since_clear * 2 >= it->second.best_segment_last_frame;
    if (target == g_selected && main_pass && a.primary_runtime != nullptr &&
        cmd_list == a.primary_runtime->get_command_queue()->get_immediate_command_list())
        copy_now(cmd_list);

    // Il riferimento e' il frame precedente (non il massimo di sempre): dopo un'inquadratura
    // piena di auto, una piu' leggera del replay deve restare il passaggio principale.
    it->second.best_segment_this_frame = std::max(it->second.best_segment_this_frame, it->second.draws_since_clear);
    it->second.draws_since_clear = 0;
    return false;
}
}

void register_events()
{
    reshade::register_event<reshade::addon_event::create_resource>(&on_create_resource);
    reshade::register_event<reshade::addon_event::init_resource>(&on_init_resource);
    reshade::register_event<reshade::addon_event::destroy_resource>(&on_destroy_resource);
    reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(&on_bind);
    reshade::register_event<reshade::addon_event::draw>(&on_draw);
    reshade::register_event<reshade::addon_event::draw_indexed>(&on_draw_indexed);
    reshade::register_event<reshade::addon_event::clear_depth_stencil_view>(&on_clear_depth);
}

void on_runtime_init(effect_runtime *runtime)
{
    if (runtime != app().primary_runtime || runtime->get_device()->get_api() != device_api::d3d11)
        return;
    g_d3d = reinterpret_cast<ID3D11Device *>(runtime->get_device()->get_native());
    runtime->get_screenshot_width_and_height(&g_screen_width, &g_screen_height);
}

void on_runtime_destroy(effect_runtime *runtime)
{
    if (runtime != app().primary_runtime)
        return;
    const std::lock_guard<std::mutex> lock(g_mutex);
    runtime->update_texture_bindings(kSemantic, resource_view{0}, resource_view{0});
    g_source_srv.Reset();
    g_source_for = 0;
    g_copy_srv.Reset();
    g_copy_rtv.Reset();
    g_copy_texture.Reset();
    g_state.Reset();
    g_vs.Reset();
    g_ps.Reset();
    g_d3d = nullptr;
    g_selected = 0;
}

void on_reloaded_effects(effect_runtime *runtime)
{
    if (runtime == app().primary_runtime && g_copy_srv != nullptr)
        bind_to_effects(runtime);
}

void on_begin_effects(effect_runtime *runtime, command_list *cmd_list)
{
    if (runtime != app().primary_runtime)
        return;
    const std::lock_guard<std::mutex> lock(g_mutex);
    const auto it = g_candidates.find(g_selected);
    if (it == g_candidates.end() || it->second.draws_since_clear == 0)
        return;
    // Se rF2 non ha cancellato il buffer dopo il passaggio principale, la copia si fa ora.
    if (!g_copied_this_frame)
        copy_now(cmd_list);
}

void on_runtime_present(effect_runtime *runtime)
{
    if (runtime != app().primary_runtime)
        return;
    const std::lock_guard<std::mutex> lock(g_mutex);

    // Scelta del depth buffer: grande come lo schermo e con piu' draw nel frame.
    uint64_t best = 0;
    uint32_t best_draws = 0;
    for (auto &[handle, candidate] : g_candidates)
    {
        const bool screen_sized = candidate.desc.texture.width == g_screen_width && candidate.desc.texture.height == g_screen_height;
        if (screen_sized && candidate.draws_this_frame > best_draws)
        {
            best = handle;
            best_draws = candidate.draws_this_frame;
        }
        if (handle == g_selected)
            g_scene_draws_last_frame = candidate.draws_this_frame;
        candidate.draws_this_frame = 0;
        candidate.best_segment_last_frame = candidate.best_segment_this_frame;
        candidate.best_segment_this_frame = 0;
    }

    if (best != 0 && best != g_selected)
    {
        g_selected = best;
        const Candidate &c = g_candidates[best];
        g_status.found = true;
        g_status.width = c.desc.texture.width;
        g_status.height = c.desc.texture.height;
        g_status.samples = c.desc.texture.samples;
        g_status.format = static_cast<uint32_t>(c.desc.texture.format);
        log::info("Profondità: depth buffer MSAA " + std::to_string(c.desc.texture.samples) + "x " +
                  std::to_string(c.desc.texture.width) + "x" + std::to_string(c.desc.texture.height) + " selezionato.");
        if (create_shaders() && prepare_targets(best, c))
            bind_to_effects(runtime);
    }
    else if (best == 0 && g_candidates.empty())
    {
        g_status.found = false;
    }

    g_last_frame_fresh = g_copied_this_frame;
    g_fresh_streak = g_copied_this_frame ? g_fresh_streak + 1 : 0;
    g_copied_this_frame = false;
}

uint32_t fresh_streak()
{
    return g_fresh_streak;
}

const Status &status()
{
    return g_status;
}

bool depth_fresh()
{
    return g_last_frame_fresh;
}


bool scene_in_this_frame()
{
    if (g_copied_this_frame)
        return true;
    const std::lock_guard<std::mutex> lock(g_mutex);
    const auto it = g_candidates.find(g_selected);
    return it != g_candidates.end() && it->second.draws_this_frame >= 50;
}

bool scene_drawn_this_frame()
{
    const std::lock_guard<std::mutex> lock(g_mutex);
    const auto it = g_candidates.find(g_selected);
    if (it == g_candidates.end())
        return false;
    // 90% delle draw del frame precedente: la scena di rF2 arriva in piu' blocchi sullo
    // stesso depth buffer, e si considera finita solo quando sono passati quasi tutti.
    const uint32_t needed = std::max<uint32_t>(50, g_scene_draws_last_frame * 9 / 10);
    if (it->second.draws_this_frame >= needed)
        return true;
    // Fuori dalla guida (replay, monitor) il numero di draw cambia molto da un'inquadratura
    // all'altra: vale anche la profondita' gia' copiata a fine passaggio principale.
    // Al volante resta solo il criterio del 90%, su cui e' tarato l'aggancio prima dell'HUD.
    const TelemetrySnapshot &telemetry = app().telemetry.snapshot();
    return g_copied_this_frame && telemetry.connected && !telemetry.in_realtime;
}
}
