#include "render/frame_hooks.hpp"
#include "app.hpp"
#include "broadcast/spout_sender.hpp"
#include "core/effects_catalog.hpp"
#include "core/log.hpp"
#include "render/depth_capture.hpp"
#include "render/mirror_grade.hpp"
#include "perf/spike_monitor.hpp"

#include <cstring>

#include <d3d11_1.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace ktv::frame
{
namespace
{
using namespace reshade::api;
using Microsoft::WRL::ComPtr;

struct BackBufferViews
{
    resource_view rtv = {0};
    resource_view rtv_srgb = {0};
};

// Stato condiviso (gli eventi del contesto immediato arrivano dal thread di rendering).
std::unordered_map<uint64_t, BackBufferViews> g_back_buffers; // risorsa -> viste nostre
device *g_device = nullptr;
command_list *g_immediate = nullptr;

bool g_hdr_requested_at_start = false;
bool g_hdr_swapchain = false; // swapchain creata da noi in R10G10B10A2
bool g_hdr_active = false;    // verificato dopo la creazione
bool g_feed_published = false; // clean feed gia' inviato in questo frame
// Fuori dalla guida l'aggancio prima dell'HUD si usa solo se rF2 arriva al bind scelto in
// quasi tutti i frame: se ci arriva a frame alterni l'interfaccia del replay sarebbe
// colorata un frame si' e uno no (sfarfallio). Altrimenti gli effetti vanno a fine frame.
uint64_t g_hook_history = 0;
bool g_hook_reliable = true;
bool out_of_car()
{
    const TelemetrySnapshot &telemetry = app().telemetry.snapshot();
    return telemetry.connected && !telemetry.in_realtime;
}

FrameStats g_current;
FrameStats g_last;

bool g_analysis_requested = false;
bool g_analysis_recording = false;
std::vector<std::string> g_analysis_lines;
int g_draws_since_bind = 0;

// Specchietto virtuale: render target fuori schermo con depth e piu' draw = scena degli
// specchietti; la texture finale RGBA8 della stessa misura viene ripassata con i colori.
struct OffscreenTarget
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t draws = 0;
};
resource g_prev_target = {0};
bool g_prev_depth = false;
uint32_t g_screen_width = 0;
uint32_t g_screen_height = 0;
std::unordered_map<uint64_t, OffscreenTarget> g_offscreen;
uint32_t g_mirror_width = 0;
uint32_t g_mirror_height = 0;
bool g_mirror_graded = false;      // in questo frame
// Misure delle texture finali RGBA8 viste in questo frame: la scena degli specchietti vale
// solo se rF2 ne prepara poi una versione RGBA8 della stessa misura
std::vector<std::pair<uint32_t, uint32_t>> g_rgba8_sizes;
uint32_t g_candidate_width = 0, g_candidate_height = 0;
int g_candidate_frames = 0;
bool g_mirror_graded_last = false; // nel frame precedente (per il menu)
std::atomic<int> g_deferred_binds{0};

ComPtr<ID3DDeviceContextState> g_effects_state; // stato pulito per eseguire gli effetti a meta' frame

// Codifica HDR10 fatta dall'add-on a fine frame (dopo HUD e specchietto)
ComPtr<ID3D11VertexShader> g_hdr_vs;
ComPtr<ID3D11PixelShader> g_hdr_ps;
ComPtr<ID3D11Buffer> g_hdr_params;
ComPtr<ID3D11Texture2D> g_hdr_copy;          // copia del back buffer, leggibile dallo shader
ComPtr<ID3D11ShaderResourceView> g_hdr_copy_srv;
bool g_hdr_shaders_failed = false;

constexpr const char kHdrShader[] = R"(
cbuffer Params : register(b0)
{
    float peak_nits;
    float paper_white;
    float expansion;
    float saturation;
};
Texture2D<float4> source : register(t0);

void vs_main(uint id : SV_VertexID, out float4 position : SV_Position)
{
    float2 uv = float2((id << 1) & 2, id & 2);
    position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float3 srgb_to_linear(float3 c)
{
    c = saturate(c);
    return lerp(c / 12.92, pow((c + 0.055) / 1.055, 2.4), step(0.04045, c));
}

float luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

float4 ps_main(float4 position : SV_Position) : SV_Target
{
    float3 lin = srgb_to_linear(source.Load(int3(position.xy, 0)).rgb);

    // Sotto il ginocchio resta a bianco carta, sopra sale verso il picco del monitor.
    float t = saturate((luma(lin) - 0.5) / 0.5);
    float boost = lerp(1.0, peak_nits / paper_white, t * t * expansion);
    float3 nits = lin * paper_white * boost;
    nits = lerp(luma(nits).xxx, nits, saturation);

    const float3x3 rec709_to_rec2020 = {
        0.6274040, 0.3292820, 0.0433136,
        0.0690970, 0.9195400, 0.0113612,
        0.0163916, 0.0880132, 0.8955950 };
    nits = min(max(mul(rec709_to_rec2020, nits), 0.0), peak_nits);

    // Codifica PQ (SMPTE ST.2084)
    const float m1 = 0.1593017578125, m2 = 78.84375, c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
    float3 y = pow(saturate(nits / 10000.0), m1);
    return float4(pow((c1 + c2 * y) / (1.0 + c3 * y), m2), 1.0);
}
)";

bool is_back_buffer(resource r)
{
    return r.handle != 0 && g_back_buffers.count(r.handle) != 0;
}

bool is_8bit_rgba(format f)
{
    switch (f)
    {
    case format::r8g8b8a8_typeless:
    case format::r8g8b8a8_unorm:
    case format::r8g8b8a8_unorm_srgb:
    case format::b8g8r8a8_typeless:
    case format::b8g8r8a8_unorm:
    case format::b8g8r8a8_unorm_srgb:
        return true;
    default:
        return false;
    }
}

// --- AutoHDR ---------------------------------------------------------------------

bool on_create_swapchain(device_api api, swapchain_desc &desc, void *)
{
    if (api != device_api::d3d11)
        return false;

    App &a = app();
    a.ensure_initialized();

    const bool wanted = a.active_variant() == Variant::Monitor && effect_setting(a.profile, "auto_hdr").enabled;
    g_hdr_requested_at_start = wanted;
    g_hdr_swapchain = false;
    if (!wanted)
        return false;

    if (desc.back_buffer.texture.samples > 1)
    {
        log::warning("AutoHDR non attivato: il back buffer di rF2 usa l'MSAA.");
        return false;
    }

    char text[160];
    std::snprintf(text, sizeof(text), "AutoHDR: swapchain originale formato %u, present mode %u, %u buffer.",
                  static_cast<unsigned>(desc.back_buffer.texture.format), desc.present_mode, desc.back_buffer_count);
    log::info(text);

    desc.back_buffer.texture.format = format::r10g10b10a2_unorm;
    // L'HDR richiede il modello "flip" di DXGI (3 = FLIP_SEQUENTIAL, 4 = FLIP_DISCARD).
    if (desc.present_mode != 3 && desc.present_mode != 4)
    {
        desc.present_mode = 4;
        desc.back_buffer_count = std::max(desc.back_buffer_count, 2u);
    }
    g_hdr_swapchain = true;
    return true;
}

bool on_create_resource_view(device *dev, resource r, resource_usage, resource_view_desc &desc)
{
    if (!g_hdr_swapchain || !is_back_buffer(r) || !is_8bit_rgba(desc.format))
        return false;
    if (dev->get_resource_desc(r).texture.format != format::r10g10b10a2_unorm)
        return false;
    desc.format = format::r10g10b10a2_unorm; // la vista del gioco segue il nuovo formato
    return true;
}

void set_hdr_color_space(swapchain *sc)
{
    auto *native = reinterpret_cast<IDXGISwapChain *>(sc->get_native());
    ComPtr<IDXGISwapChain3> sc3;
    if (native == nullptr || FAILED(native->QueryInterface(IID_PPV_ARGS(&sc3))))
    {
        log::warning("AutoHDR: IDXGISwapChain3 non disponibile.");
        return;
    }
    UINT support = 0;
    const DXGI_COLOR_SPACE_TYPE hdr10 = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
    if (FAILED(sc3->CheckColorSpaceSupport(hdr10, &support)) || (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) == 0)
    {
        log::warning("AutoHDR: il monitor non accetta HDR10 (HDR attivo in Windows?).");
        return;
    }
    if (SUCCEEDED(sc3->SetColorSpace1(hdr10)))
    {
        g_hdr_active = true;
        log::info("AutoHDR attivo: swapchain HDR10 (ST.2084, Rec.2020).");
    }
}

// --- Swapchain e back buffer -----------------------------------------------------

void on_init_swapchain(swapchain *sc, bool)
{
    device *dev = sc->get_device();
    if (dev->get_api() != device_api::d3d11)
        return;
    g_device = dev;

    for (uint32_t i = 0; i < sc->get_back_buffer_count(); ++i)
    {
        const resource bb = sc->get_back_buffer(i);
        const format base = dev->get_resource_desc(bb).texture.format;
        g_screen_width = dev->get_resource_desc(bb).texture.width;
        g_screen_height = dev->get_resource_desc(bb).texture.height;
        BackBufferViews views;
        dev->create_resource_view(bb, resource_usage::render_target, resource_view_desc(format_to_default_typed(base, 0), 0, 1, 0, 1), &views.rtv);
        dev->create_resource_view(bb, resource_usage::render_target, resource_view_desc(format_to_default_typed(base, 1), 0, 1, 0, 1), &views.rtv_srgb);
        g_back_buffers[bb.handle] = views;
    }

    if (g_hdr_swapchain)
    {
        const resource bb = sc->get_back_buffer(0);
        if (dev->get_resource_desc(bb).texture.format == format::r10g10b10a2_unorm)
            set_hdr_color_space(sc);
        else
            log::warning("AutoHDR: il back buffer non e' in R10G10B10A2, HDR non attivato.");
    }
}

void on_destroy_swapchain(swapchain *sc, bool)
{
    device *dev = sc->get_device();
    for (uint32_t i = 0; i < sc->get_back_buffer_count(); ++i)
    {
        const auto it = g_back_buffers.find(sc->get_back_buffer(i).handle);
        if (it == g_back_buffers.end())
            continue;
        if (it->second.rtv.handle != 0) dev->destroy_resource_view(it->second.rtv);
        if (it->second.rtv_srgb.handle != 0) dev->destroy_resource_view(it->second.rtv_srgb);
        g_back_buffers.erase(it);
    }
}

// --- Specchietto virtuale con i colori della scena ----------------------------------------

float effect_param(const EffectSetting &setting, const char *effect_id, const char *uniform, float fallback)
{
    if (const EffectInfo *info = find_effect(effect_id))
        for (const ParamInfo &param : info->params)
            if (std::string_view(param.uniform) == uniform)
                return param_value(setting, param);
    return fallback;
}

void grade_mirror(command_list *cmd_list, resource texture)
{
    const App &a = app();
    effect_runtime *rt = a.primary_runtime;
    if (rt == nullptr || !a.profile.features.mirror_grade || a.active_variant() != Variant::Monitor)
        return;
    // Stesse condizioni della scena: se tonemap e grade sono spenti, lo specchietto resta com'e'
    const bool tonemap_on = a.pipeline.is_active(rt, "tonemap");
    const bool grade_on = a.pipeline.is_active(rt, "grade");
    if (!tonemap_on && !grade_on)
        return;

    mirror::GradeParams p;
    if (tonemap_on)
    {
        const EffectSetting s = effect_setting(a.profile, "tonemap");
        p.tonemap_intensity = s.intensity;
        p.tonemap_mode = effect_param(s, "tonemap", "KTV_Mode", 0.0f);
        p.exposure_ev = effect_param(s, "tonemap", "KTV_Exposure", 0.0f);
        p.hdr_recovery = effect_param(s, "tonemap", "KTV_HdrRecovery", 0.6f);
        p.auto_exposure = effect_param(s, "tonemap", "KTV_AutoExposure", 0.5f);
    }
    if (grade_on)
    {
        const EffectSetting s = effect_setting(a.profile, "grade");
        p.grade_intensity = s.intensity;
        p.temperature = effect_param(s, "grade", "KTV_Temperature", 0.0f);
        p.tint = effect_param(s, "grade", "KTV_Tint", 0.0f);
        p.vibrance = effect_param(s, "grade", "KTV_Vibrance", 0.25f);
        p.saturation = effect_param(s, "grade", "KTV_Saturation", 1.0f);
        p.contrast = effect_param(s, "grade", "KTV_Contrast", 0.2f);
        p.lut_index = effect_param(s, "grade", "KTV_LutIndex", 0.0f);
        p.lut_amount = effect_param(s, "grade", "KTV_LutAmount", 1.0f);
    }

    // LUT e luminanza adattata: le stesse texture usate dagli shader della scena
    resource_view lut = {0}, lut_srgb = {0}, adapt = {0}, adapt_srgb = {0};
    const effect_texture_variable lut_var = rt->find_texture_variable("KarmaTV_50_Grade.fx", "KTV_LutAtlasTex");
    if (lut_var.handle != 0)
        rt->get_texture_binding(lut_var, &lut, &lut_srgb);
    const effect_texture_variable adapt_var = rt->find_texture_variable("KarmaTV_40_Tonemap.fx", "KTV_AdaptTex");
    if (adapt_var.handle != 0)
        rt->get_texture_binding(adapt_var, &adapt, &adapt_srgb);

    g_mirror_graded = mirror::apply(reinterpret_cast<ID3D11DeviceContext *>(cmd_list->get_native()),
                                    reinterpret_cast<ID3D11Texture2D *>(texture.handle), p,
                                    reinterpret_cast<ID3D11ShaderResourceView *>(lut.handle),
                                    reinterpret_cast<ID3D11ShaderResourceView *>(adapt.handle));
}

// Chiamata quando rF2 passa a un altro render target: il precedente ha finito le sue draw
void note_finished_target(command_list *cmd_list, int draws)
{
    if (g_prev_target.handle == 0 || draws <= 0 || g_device == nullptr)
        return;
    const resource_desc desc = g_device->get_resource_desc(g_prev_target);
    const uint32_t w = desc.texture.width;
    const uint32_t h = desc.texture.height;
    const bool screen_sized = w == g_screen_width && h == g_screen_height;
    if (g_prev_depth)
    {
        if (!screen_sized && w >= 256 && h >= 64)
        {
            OffscreenTarget &t = g_offscreen[g_prev_target.handle];
            t.width = w;
            t.height = h;
            t.draws += static_cast<uint32_t>(draws);
        }
        return;
    }
    if (desc.texture.samples == 1 && is_8bit_rgba(desc.texture.format) && !screen_sized && g_rgba8_sizes.size() < 64)
        g_rgba8_sizes.emplace_back(w, h);
    if (!g_mirror_graded && g_mirror_width != 0 && w == g_mirror_width && h == g_mirror_height &&
        desc.texture.samples == 1 && is_8bit_rgba(desc.texture.format))
        grade_mirror(cmd_list, g_prev_target);
}

// --- Clean feed -----------------------------------------------------------------------

void publish_feed(command_list *cmd_list, resource bb)
{
    if (bb.handle == 0)
        return;
    // In D3D11 gli handle di ReShade sono i puntatori nativi
    spout::publish(reinterpret_cast<ID3D11DeviceContext *>(cmd_list->get_native()),
                   reinterpret_cast<ID3D11Resource *>(bb.handle));
}

// --- Esclusione dell'HUD -------------------------------------------------------------

void render_effects_now(effect_runtime *runtime, command_list *cmd_list, resource bb)
{
    const auto views = g_back_buffers.find(bb.handle);
    if (views == g_back_buffers.end() || views->second.rtv.handle == 0)
        return;

    auto *context = reinterpret_cast<ID3D11DeviceContext *>(cmd_list->get_native());
    ComPtr<ID3D11DeviceContext1> context1;
    if (FAILED(context->QueryInterface(IID_PPV_ARGS(&context1))))
        return;

    if (g_effects_state == nullptr)
    {
        ComPtr<ID3D11Device> d3d;
        context->GetDevice(&d3d);
        ComPtr<ID3D11Device1> d3d1;
        if (FAILED(d3d.As(&d3d1)))
            return;
        const D3D_FEATURE_LEVEL level = d3d->GetFeatureLevel();
        if (FAILED(d3d1->CreateDeviceContextState(0, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1), nullptr, &g_effects_state)))
        {
            log::warning("Esclusione HUD: impossibile creare lo stato D3D11 di appoggio.");
            return;
        }
    }

    // Lo stato di rF2 viene messo da parte e ripristinato identico dopo gli effetti.
    ID3DDeviceContextState *previous = nullptr;
    context1->SwapDeviceContextState(g_effects_state.Get(), &previous);
    runtime->render_effects(cmd_list, views->second.rtv, views->second.rtv_srgb);
    context1->SwapDeviceContextState(previous, nullptr);
    if (previous != nullptr)
        previous->Release();
}

// --- Filtro anisotropico e LOD forzati --------------------------------------------

int g_samplers_overridden = 0;

bool on_create_sampler(device *dev, sampler_desc &desc)
{
    if (dev->get_api() != device_api::d3d11)
        return false;
    App &a = app();
    a.ensure_initialized();

    if (!a.profile.features.af_override)
        return false;

    // Ombre (confronto: rF2 le filtra gia' in bilineare) e lookup puntuali non si toccano
    const uint32_t filter = static_cast<uint32_t>(desc.filter);
    const bool comparison = (filter & 0x80) != 0;
    const bool point = desc.filter == filter_mode::min_mag_mip_point; // testo, interfaccia, lookup
    if (comparison || point)
        return false;

    desc.filter = filter_mode::anisotropic;
    desc.max_anisotropy = static_cast<float>(std::clamp(a.profile.features.af_level, 2, 16));
    desc.mip_lod_bias += std::clamp(a.profile.features.lod_bias, -2.0f, 2.0f);
    if (++g_samplers_overridden == 1)
        log::info("Filtro anisotropico forzato sulle texture di rF2 (" + std::to_string(a.profile.features.af_level) + "x).");
    return true;
}

// --- Codifica HDR10 a fine frame -------------------------------------------------

bool create_hdr_resources(ID3D11Device *d3d)
{
    if (g_hdr_vs != nullptr)
        return true;
    if (g_hdr_shaders_failed)
        return false;

    using D3DCompileFn = HRESULT(WINAPI *)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO *, ID3DInclude *, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);
    HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
    const auto compile = compiler != nullptr ? reinterpret_cast<D3DCompileFn>(GetProcAddress(compiler, "D3DCompile")) : nullptr;
    ComPtr<ID3DBlob> vs_code, ps_code, errors;
    const bool ok = compile != nullptr &&
        SUCCEEDED(compile(kHdrShader, sizeof(kHdrShader) - 1, "karmatv_hdr", nullptr, nullptr, "vs_main", "vs_5_0", 0, 0, &vs_code, &errors)) &&
        SUCCEEDED(compile(kHdrShader, sizeof(kHdrShader) - 1, "karmatv_hdr", nullptr, nullptr, "ps_main", "ps_5_0", 0, 0, &ps_code, &errors)) &&
        SUCCEEDED(d3d->CreateVertexShader(vs_code->GetBufferPointer(), vs_code->GetBufferSize(), nullptr, &g_hdr_vs)) &&
        SUCCEEDED(d3d->CreatePixelShader(ps_code->GetBufferPointer(), ps_code->GetBufferSize(), nullptr, &g_hdr_ps));
    if (!ok)
    {
        g_hdr_shaders_failed = true;
        g_hdr_vs.Reset();
        g_hdr_ps.Reset();
        log::error(std::string("AutoHDR: compilazione dello shader di codifica fallita. ") +
                   (errors != nullptr ? static_cast<const char *>(errors->GetBufferPointer()) : ""));
        return false;
    }

    D3D11_BUFFER_DESC params = {};
    params.ByteWidth = 16;
    params.Usage = D3D11_USAGE_DYNAMIC;
    params.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    params.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(d3d->CreateBuffer(&params, nullptr, &g_hdr_params)))
    {
        g_hdr_shaders_failed = true;
        return false;
    }
    log::info("AutoHDR: codifica HDR10 a fine frame pronta (HUD escluso dagli effetti ma codificato in HDR).");
    return true;
}

ID3DDeviceContextState *clean_state(ID3D11DeviceContext *context)
{
    if (g_effects_state == nullptr)
    {
        ComPtr<ID3D11Device> d3d;
        context->GetDevice(&d3d);
        ComPtr<ID3D11Device1> d3d1;
        if (FAILED(d3d.As(&d3d1)))
            return nullptr;
        const D3D_FEATURE_LEVEL level = d3d->GetFeatureLevel();
        if (FAILED(d3d1->CreateDeviceContextState(0, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1), nullptr, &g_effects_state)))
            return nullptr;
    }
    return g_effects_state.Get();
}

void encode_hdr(command_list *cmd_list, resource bb)
{
    const auto views = g_back_buffers.find(bb.handle);
    if (views == g_back_buffers.end() || views->second.rtv.handle == 0)
        return;

    auto *context = reinterpret_cast<ID3D11DeviceContext *>(cmd_list->get_native());
    ComPtr<ID3D11Device> d3d;
    context->GetDevice(&d3d);
    if (!create_hdr_resources(d3d.Get()))
        return;
    ComPtr<ID3D11DeviceContext1> context1;
    ID3DDeviceContextState *state = clean_state(context);
    if (state == nullptr || FAILED(context->QueryInterface(IID_PPV_ARGS(&context1))))
        return;

    auto *back_buffer = reinterpret_cast<ID3D11Texture2D *>(bb.handle);
    D3D11_TEXTURE2D_DESC desc = {};
    back_buffer->GetDesc(&desc);

    // Copia del back buffer (che non e' leggibile dagli shader) della stessa misura
    D3D11_TEXTURE2D_DESC copy_desc = {};
    if (g_hdr_copy != nullptr)
        g_hdr_copy->GetDesc(&copy_desc);
    if (g_hdr_copy == nullptr || copy_desc.Width != desc.Width || copy_desc.Height != desc.Height || copy_desc.Format != desc.Format)
    {
        g_hdr_copy.Reset();
        g_hdr_copy_srv.Reset();
        copy_desc = desc;
        copy_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        copy_desc.MiscFlags = 0;
        copy_desc.CPUAccessFlags = 0;
        copy_desc.Usage = D3D11_USAGE_DEFAULT;
        if (FAILED(d3d->CreateTexture2D(&copy_desc, nullptr, &g_hdr_copy)) ||
            FAILED(d3d->CreateShaderResourceView(g_hdr_copy.Get(), nullptr, &g_hdr_copy_srv)))
        {
            log::error("AutoHDR: impossibile creare la copia del back buffer.");
            g_hdr_copy.Reset();
            return;
        }
    }

    // Parametri dal profilo (stessi del menu: picco, bianco carta, espansione, saturazione)
    const App &a = app();
    const EffectSetting setting = effect_setting(a.profile, "auto_hdr");
    float values[4] = {1000.0f, 200.0f, setting.intensity, 1.0f};
    if (const EffectInfo *info = find_effect("auto_hdr"))
    {
        for (const ParamInfo &param : info->params)
        {
            const std::string_view name = param.uniform;
            if (name == "KTV_PeakNits") values[0] = param_value(setting, param);
            else if (name == "KTV_PaperWhite") values[1] = param_value(setting, param);
            else if (name == "KTV_Saturation") values[3] = param_value(setting, param);
        }
    }
    // Effetti spenti o profilo "Originale rF2": solo il trasporto in HDR, senza
    // espansione delle alte luci ne' saturazione (l'immagine di rF2 com'e').
    if (!a.master_enabled || a.profile.name == "Originale rF2")
    {
        values[2] = 0.0f;
        values[3] = 1.0f;
    }

    ID3DDeviceContextState *previous = nullptr;
    context1->SwapDeviceContextState(state, &previous);

    context1->CopyResource(g_hdr_copy.Get(), back_buffer);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(context1->Map(g_hdr_params.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        std::memcpy(mapped.pData, values, sizeof(values));
        context1->Unmap(g_hdr_params.Get(), 0);
    }

    auto *rtv = reinterpret_cast<ID3D11RenderTargetView *>(views->second.rtv.handle);
    ID3D11ShaderResourceView *srv = g_hdr_copy_srv.Get();
    ID3D11Buffer *cb = g_hdr_params.Get();
    const D3D11_VIEWPORT viewport = {0.0f, 0.0f, static_cast<float>(desc.Width), static_cast<float>(desc.Height), 0.0f, 1.0f};
    context1->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context1->IASetInputLayout(nullptr);
    context1->VSSetShader(g_hdr_vs.Get(), nullptr, 0);
    context1->PSSetShader(g_hdr_ps.Get(), nullptr, 0);
    context1->PSSetConstantBuffers(0, 1, &cb);
    context1->PSSetShaderResources(0, 1, &srv);
    context1->OMSetRenderTargets(1, &rtv, nullptr);
    context1->RSSetViewports(1, &viewport);
    context1->Draw(3, 0);
    ID3D11ShaderResourceView *none = nullptr;
    context1->PSSetShaderResources(0, 1, &none);

    context1->SwapDeviceContextState(previous, nullptr);
    if (previous != nullptr)
        previous->Release();
}

// Prima della presentazione (e prima dell'overlay di ReShade): se gli effetti non sono
// stati agganciati prima dell'HUD li si esegue ora, poi si codifica l'intera immagine
// (scena + specchietto + HUD) in HDR10.
void on_present(command_queue *queue, swapchain *sc, const rect *, const rect *, uint32_t, const rect *)
{
    App &a = app();
    // Solo la swapchain del runtime principale (stessa finestra)
    if (a.primary_runtime == nullptr || sc->get_hwnd() != a.primary_runtime->get_hwnd())
        return;

    command_list *cmd_list = queue->get_immediate_command_list();
    const resource bb = sc->get_current_back_buffer();

    // Clean feed: se l'aggancio prima dell'HUD non c'e' stato (menu, caricamenti) si invia
    // l'immagine intera, cosi' OBS non resta fermo sull'ultimo frame
    if (a.profile.features.clean_feed && a.active_variant() == Variant::Monitor && !g_feed_published)
    {
        g_feed_published = true;
        publish_feed(cmd_list, bb);
    }

    // Fuori dalla guida (menu, replay con l'interfaccia, monitor): se in questo frame
    // rF2 non ha disegnato la scena 3D, o gli effetti non sono partiti prima dell'HUD,
    // sullo schermo c'e' soprattutto interfaccia. Gli effetti si saltano per questo frame,
    // altrimenti a frame alterni colorerebbero il menu (sfarfallio). La chiamata con
    // target vuoto segna il frame come gia' elaborato e ReShade non applica nulla.
    const bool outside = out_of_car();
    const bool scene_drawn = depth::scene_in_this_frame();
    const bool skip_effects = outside && !g_current.hook_fired && !scene_drawn;
    if (skip_effects)
        a.primary_runtime->render_effects(cmd_list, resource_view{0}, resource_view{0});

    // Diagnostica fuori dalla guida: ogni 10 s quanti frame hanno la scena 3D, quanti
    // l'aggancio prima dell'HUD e quanti sono stati saltati (massimo 60 righe)
    static int diag_frames = 0, diag_scene = 0, diag_hook = 0, diag_skipped = 0, diag_lines = 0;
    static double diag_start = 0.0;
    if (outside && a.master_enabled)
    {
        const double now = now_seconds();
        if (diag_frames == 0)
            diag_start = now;
        ++diag_frames;
        diag_scene += scene_drawn ? 1 : 0;
        diag_hook += g_current.hook_fired ? 1 : 0;
        diag_skipped += skip_effects ? 1 : 0;
        if (now - diag_start >= 10.0)
        {
            if (diag_lines < 60)
            {
                ++diag_lines;
                log::info("Fuori dalla guida, ultimi 10 s: " + std::to_string(diag_frames) + " frame, scena 3D in " +
                          std::to_string(diag_scene) + ", effetti prima dell'HUD in " + std::to_string(diag_hook) +
                          ", saltati " + std::to_string(diag_skipped) + (a.pipeline.replay_detected() ? " (replay)" : " (menu)") +
                          (g_hook_reliable ? "" : ", aggancio irregolare"));
            }
            diag_frames = diag_scene = diag_hook = diag_skipped = 0;
        }
    }
    else
    {
        diag_frames = diag_scene = diag_hook = diag_skipped = 0;
    }

    if (!g_hdr_active)
        return;
    if (!g_current.hook_fired && !skip_effects)
        render_effects_now(a.primary_runtime, cmd_list, bb);
    encode_hdr(cmd_list, bb);
}

void on_bind_render_targets(command_list *cmd_list, uint32_t count, const resource_view *rtvs, resource_view dsv)
{
    if (cmd_list != g_immediate || g_device == nullptr)
    {
        if (g_analysis_recording)
            ++g_deferred_binds;
        return;
    }

    const resource target = (count > 0 && rtvs[0].handle != 0) ? g_device->get_resource_from_view(rtvs[0]) : resource{0};
    const bool back_buffer = is_back_buffer(target);

    if (target.handle != g_prev_target.handle)
    {
        note_finished_target(cmd_list, g_draws_since_bind);
        g_prev_target = target;
        g_prev_depth = dsv.handle != 0;
    }

    if (g_analysis_recording && g_analysis_lines.size() < 400)
    {
        // Per gli altri target si annotano dimensioni, formato e campioni: servono a
        // riconoscere le texture degli specchietti (piu' piccole dello schermo, con depth)
        char target_text[96] = "nessun target";
        if (back_buffer)
            std::snprintf(target_text, sizeof(target_text), "BACK BUFFER");
        else if (target.handle != 0)
        {
            const resource_desc desc = g_device->get_resource_desc(target);
            std::snprintf(target_text, sizeof(target_text), "target %ux%u fmt %u x%u [%016llx]",
                          desc.texture.width, desc.texture.height, static_cast<unsigned>(desc.texture.format),
                          static_cast<unsigned>(desc.texture.samples), static_cast<unsigned long long>(target.handle));
        }
        char line[200];
        std::snprintf(line, sizeof(line), "  bind %3zu: %s%s, draw prima: %d",
                      g_analysis_lines.size(), target_text, dsv.handle != 0 ? " + depth" : "", g_draws_since_bind);
        g_analysis_lines.emplace_back(line);
    }
    g_draws_since_bind = 0;

    if (!back_buffer)
        return;
    ++g_current.backbuffer_binds;
    if (dsv.handle != 0)
        return;
    ++g_current.backbuffer_binds_no_depth;

    // In rF2 il back buffer viene legato anche a vuoto durante la preparazione del frame:
    // si contano solo i bind successivi alla scena principale (1 = composizione della
    // scena, 2 = prima dello specchietto virtuale e dell'HUD).
    if (!depth::scene_drawn_this_frame())
        return;
    ++g_current.backbuffer_binds_after_scene;

    // Aggancio prima dell'HUD: effetti e clean feed per OBS
    const App &a = app();
    const int hook = a.profile.features.hud_hook_index;
    if (hook <= 0 || a.active_variant() != Variant::Monitor || a.primary_runtime == nullptr ||
        g_current.backbuffer_binds_after_scene != hook)
        return;
    g_current.hook_reached = true;
    if (out_of_car() && !g_hook_reliable)
        return;
    if (a.master_enabled && a.profile.features.hud_exclusion && !g_current.hook_fired)
    {
        g_current.hook_fired = true;
        render_effects_now(a.primary_runtime, cmd_list, target);
    }
    if (a.profile.features.clean_feed && !g_feed_published)
    {
        g_feed_published = true;
        publish_feed(cmd_list, target); // scena con gli effetti, prima di specchietto e HUD
    }
}

bool on_draw(command_list *cmd_list, uint32_t, uint32_t, uint32_t, uint32_t)
{
    if (cmd_list == g_immediate)
    {
        ++g_current.draws;
        ++g_draws_since_bind;
    }
    return false;
}

bool on_draw_indexed(command_list *cmd_list, uint32_t, uint32_t, uint32_t, int32_t, uint32_t)
{
    return on_draw(cmd_list, 0, 0, 0, 0);
}
}

void register_events()
{
    reshade::register_event<reshade::addon_event::create_swapchain>(&on_create_swapchain);
    reshade::register_event<reshade::addon_event::init_swapchain>(&on_init_swapchain);
    reshade::register_event<reshade::addon_event::destroy_swapchain>(&on_destroy_swapchain);
    reshade::register_event<reshade::addon_event::create_resource_view>(&on_create_resource_view);
    reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(&on_bind_render_targets);
    reshade::register_event<reshade::addon_event::draw>(&on_draw);
    reshade::register_event<reshade::addon_event::draw_indexed>(&on_draw_indexed);
    reshade::register_event<reshade::addon_event::present>(&on_present);
    reshade::register_event<reshade::addon_event::create_sampler>(&on_create_sampler);
}

void on_runtime_init(effect_runtime *runtime)
{
    if (runtime != app().primary_runtime)
        return;
    g_immediate = runtime->get_command_queue()->get_immediate_command_list();
    if (g_hdr_active)
        runtime->set_color_space(color_space::hdr10_st2084);
}

void on_runtime_destroy(effect_runtime *runtime)
{
    if (runtime != app().primary_runtime)
        return;
    g_immediate = nullptr;
    spout::shutdown();
    mirror::release();
    g_effects_state.Reset();
    g_hdr_copy_srv.Reset();
    g_hdr_copy.Reset();
    g_hdr_params.Reset();
    g_hdr_ps.Reset();
    g_hdr_vs.Reset();
}

void on_runtime_present(effect_runtime *runtime)
{
    if (runtime != app().primary_runtime)
        return;

    if (g_analysis_recording)
    {
        g_analysis_recording = false;
        log::info("Analisi del frame: " + std::to_string(g_analysis_lines.size()) + " bind sul contesto immediato, " +
                  std::to_string(g_deferred_binds.load()) + " su contesti differiti, " +
                  std::to_string(g_current.draws) + " draw, " +
                  std::to_string(g_current.backbuffer_binds_no_depth) + " bind del back buffer senza depth.");
        for (const std::string &line : g_analysis_lines)
            log::info(line);
        g_analysis_lines.clear();
    }

    g_hook_history = (g_hook_history << 1) | (g_current.hook_reached ? 1u : 0u);
    {
        int reached = 0;
        for (uint64_t bits = g_hook_history; bits != 0; bits &= bits - 1)
            ++reached;
        const bool reliable = g_hook_reliable ? reached >= 56 : reached >= 63;
        if (reliable != g_hook_reliable && out_of_car())
            log::info(reliable ? "Fuori dalla guida: effetti di nuovo prima dell'HUD." :
                                 "Fuori dalla guida: aggancio prima dell'HUD irregolare, effetti a fine frame.");
        g_hook_reliable = reliable;
    }
    g_last = g_current;
    g_current = FrameStats{};
    g_feed_published = false;

    // Scena degli specchietti = render target fuori schermo con piu' draw (almeno 50) di cui
    // rF2 prepara anche una versione finale RGBA8 della stessa misura. Si cambia solo se lo
    // stesso candidato vince per 60 frame di fila: niente salti (e niente log) a ogni frame.
    uint32_t best_draws = 50;
    uint32_t mirror_w = 0, mirror_h = 0;
    for (const auto &[handle, t] : g_offscreen)
    {
        const bool has_final = std::find(g_rgba8_sizes.begin(), g_rgba8_sizes.end(), std::make_pair(t.width, t.height)) != g_rgba8_sizes.end();
        if (has_final && t.draws >= best_draws)
        {
            best_draws = t.draws;
            mirror_w = t.width;
            mirror_h = t.height;
        }
    }
    if (mirror_w != 0 && (mirror_w != g_mirror_width || mirror_h != g_mirror_height))
    {
        if (mirror_w == g_candidate_width && mirror_h == g_candidate_height)
            ++g_candidate_frames;
        else
        {
            g_candidate_width = mirror_w;
            g_candidate_height = mirror_h;
            g_candidate_frames = 1;
        }
        if (g_candidate_frames >= 60)
        {
            g_mirror_width = mirror_w;
            g_mirror_height = mirror_h;
            g_candidate_frames = 0;
            log::info("Specchietti riconosciuti: " + std::to_string(mirror_w) + "x" + std::to_string(mirror_h) + " (" + std::to_string(best_draws) + " draw).");
            spikes::note("specchietti riconosciuti");
        }
    }
    else
    {
        g_candidate_frames = 0;
    }
    g_offscreen.clear();
    g_rgba8_sizes.clear();
    g_mirror_graded_last = g_mirror_graded;
    g_mirror_graded = false;
    g_prev_target = {0};
    if (!app().profile.features.clean_feed && spout::active())
        spout::shutdown();
    g_draws_since_bind = 0;

    // L'analisi parte all'inizio del frame successivo, per registrarlo intero.
    if (g_analysis_requested)
    {
        g_analysis_requested = false;
        g_analysis_recording = true;
        g_deferred_binds = 0;
    }
}

void request_analysis()
{
    g_analysis_requested = true;
}

bool analysis_pending()
{
    return g_analysis_requested || g_analysis_recording;
}

const FrameStats &last_frame()
{
    return g_last;
}

bool mirror_graded()
{
    return g_mirror_graded_last;
}

bool hdr_active()
{
    return g_hdr_active;
}

bool hdr_restart_needed()
{
    App &a = app();
    const bool wanted = a.active_variant() == Variant::Monitor && effect_setting(a.profile, "auto_hdr").enabled;
    return wanted != g_hdr_requested_at_start;
}
}
