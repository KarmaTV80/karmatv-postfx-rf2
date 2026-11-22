#include "render/mirror_grade.hpp"
#include "core/log.hpp"

#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cstring>
#include <string>
#include <unordered_map>

namespace ktv::mirror
{
namespace
{
using Microsoft::WRL::ComPtr;

// Stesse formule di KarmaTV_40_Tonemap.fx e KarmaTV_50_Grade.fx
constexpr const char kShader[] = R"(
cbuffer Params : register(b0)
{
    float4 tm;   // intensita', modo, esposizione (EV), recupero alte luci
    float4 tm2;  // esposizione automatica, adattamento disponibile
    float4 gr;   // intensita', temperatura, tinta, vibrance
    float4 gr2;  // saturazione, contrasto, indice LUT, intensita' LUT
    float4 misc; // LUT disponibile
};
Texture2D<float4> source : register(t0);
Texture2D<float4> lut_atlas : register(t1);
Texture2D<float> adapted : register(t2);
SamplerState linear_clamp : register(s0);

void vs_main(uint id : SV_VertexID, out float4 position : SV_Position)
{
    float2 uv = float2(id == 2 ? 2.0 : 0.0, id == 1 ? 2.0 : 0.0);
    position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

float3 inverse_tonemap(float3 color, float strength)
{
    float3 linear_color = pow(saturate(color), 2.2);
    return linear_color / max(1.0 - linear_color * strength, 0.05);
}

float3 aces(float3 x) { return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14)); }

float3 agx_contrast(float3 x)
{
    float3 x2 = x * x;
    float3 x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

float3 agx(float3 c)
{
    const float3x3 m = float3x3(0.842479062253094, 0.0423282422610123, 0.0423756549057051,
                                0.0784335999999992, 0.878468636469772, 0.0784336,
                                0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const float3x3 mi = float3x3(1.19687900512017, -0.0528968517574562, -0.0529716355144438,
                                 -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
                                 -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float lo = -12.47393;
    const float hi = 4.026069;
    float3 v = mul(c, m);
    v = clamp(log2(max(v, 1e-10)), lo, hi);
    v = (v - lo) / (hi - lo);
    return saturate(mul(agx_contrast(v), mi));
}

float3 apply_lut(float3 color, float index)
{
    const float size = 32.0;
    color = saturate(color);
    float blue = color.b * (size - 1.0);
    float slice = floor(blue);
    float row = (color.g * (size - 1.0) + 0.5 + index * size) / (size * 10.0);
    float2 uv0 = float2((color.r * (size - 1.0) + 0.5) / (size * size) + slice / size, row);
    float2 uv1 = uv0 + float2(1.0 / size, 0.0);
    return lerp(lut_atlas.SampleLevel(linear_clamp, uv0, 0).rgb, lut_atlas.SampleLevel(linear_clamp, uv1, 0).rgb, blue - slice);
}

float4 ps_main(float4 position : SV_Position) : SV_Target
{
    float4 original = source.Load(int3(position.xy, 0));
    float3 color = original.rgb;

    if (tm.x > 0.0)
    {
        float exposure = exp2(tm.z);
        if (tm2.y > 0.0)
        {
            float a = max(adapted.Load(int3(0, 0, 0)), 1e-4);
            exposure *= lerp(1.0, clamp(0.18 / a, 0.35, 3.0), tm2.x);
        }
        float3 hdr = inverse_tonemap(color, tm.w) * exposure;
        float3 mapped;
        if (tm.y < 0.5)
            mapped = pow(saturate(hdr / (1.0 + hdr * tm.w)), 1.0 / 2.2);
        else if (tm.y < 1.5)
            mapped = pow(aces(hdr), 1.0 / 2.2);
        else
            mapped = agx(hdr);
        color = lerp(color, mapped, tm.x);
    }

    if (gr.x > 0.0)
    {
        float3 c = color * float3(1.0 + 0.1 * gr.y, 1.0 - 0.1 * gr.z, 1.0 - 0.1 * gr.y);
        float l = luma(c);
        float s = max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b));
        c = lerp(l.xxx, c, 1.0 + gr.w * (1.0 - s));
        c = lerp(l.xxx, c, gr2.x);
        c = saturate(c);
        c = lerp(c, c * c * (3.0 - 2.0 * c), gr2.y);
        if (gr2.z > 0.5 && misc.x > 0.0)
            c = lerp(c, apply_lut(c, gr2.z), gr2.w);
        color = lerp(color, c, gr.x);
    }
    return float4(saturate(color), original.a);
}
)";

struct Constants
{
    float tm[4];
    float tm2[4];
    float gr[4];
    float gr2[4];
    float misc[4];
};

ComPtr<ID3D11VertexShader> g_vs;
ComPtr<ID3D11PixelShader> g_ps;
ComPtr<ID3D11Buffer> g_constants;
ComPtr<ID3D11SamplerState> g_sampler;
ComPtr<ID3DDeviceContextState> g_state;
ComPtr<ID3D11Texture2D> g_copy;
ComPtr<ID3D11ShaderResourceView> g_copy_srv;
std::unordered_map<ID3D11Texture2D *, ComPtr<ID3D11RenderTargetView>> g_rtvs;
bool g_failed = false;

bool create_resources(ID3D11Device *device)
{
    if (g_ps != nullptr)
        return true;
    if (g_failed)
        return false;
    g_failed = true; // ripristinato solo se tutto va a buon fine

    using D3DCompileFn = HRESULT(WINAPI *)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO *, ID3DInclude *, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);
    HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
    const auto compile = compiler != nullptr ? reinterpret_cast<D3DCompileFn>(GetProcAddress(compiler, "D3DCompile")) : nullptr;
    ComPtr<ID3DBlob> vs_code, ps_code, errors;
    if (compile == nullptr ||
        FAILED(compile(kShader, sizeof(kShader) - 1, "karmatv_mirror", nullptr, nullptr, "vs_main", "vs_5_0", 0, 0, &vs_code, &errors)) ||
        FAILED(compile(kShader, sizeof(kShader) - 1, "karmatv_mirror", nullptr, nullptr, "ps_main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps_code, &errors)) ||
        FAILED(device->CreateVertexShader(vs_code->GetBufferPointer(), vs_code->GetBufferSize(), nullptr, &g_vs)) ||
        FAILED(device->CreatePixelShader(ps_code->GetBufferPointer(), ps_code->GetBufferSize(), nullptr, &g_ps)))
    {
        log::error(std::string("Specchietti: compilazione dello shader fallita. ") +
                   (errors != nullptr ? static_cast<const char *>(errors->GetBufferPointer()) : ""));
        g_vs.Reset();
        g_ps.Reset();
        return false;
    }

    D3D11_BUFFER_DESC cb = {};
    cb.ByteWidth = sizeof(Constants);
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    D3D11_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;

    ComPtr<ID3D11Device1> device1;
    const D3D_FEATURE_LEVEL level = device->GetFeatureLevel();
    if (FAILED(device->CreateBuffer(&cb, nullptr, &g_constants)) || FAILED(device->CreateSamplerState(&sampler, &g_sampler)) ||
        FAILED(device->QueryInterface(IID_PPV_ARGS(&device1))) ||
        FAILED(device1->CreateDeviceContextState(0, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1), nullptr, &g_state)))
    {
        log::error("Specchietti: impossibile creare le risorse del passaggio colore.");
        g_vs.Reset();
        g_ps.Reset();
        return false;
    }
    g_failed = false;
    log::info("Specchietto virtuale: stessi colori della scena attivi.");
    return true;
}

DXGI_FORMAT view_format(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
    default: return format;
    }
}
}

bool apply(ID3D11DeviceContext *context, ID3D11Texture2D *texture, const GradeParams &p,
           ID3D11ShaderResourceView *lut, ID3D11ShaderResourceView *adapt)
{
    if (context == nullptr || texture == nullptr)
        return false;
    ComPtr<ID3D11Device> device;
    context->GetDevice(&device);
    ComPtr<ID3D11DeviceContext1> context1;
    if (!create_resources(device.Get()) || FAILED(context->QueryInterface(IID_PPV_ARGS(&context1))))
        return false;

    D3D11_TEXTURE2D_DESC desc = {};
    texture->GetDesc(&desc);
    if (desc.SampleDesc.Count != 1 || (desc.BindFlags & D3D11_BIND_RENDER_TARGET) == 0)
        return false;

    // Copia leggibile della texture (stessa misura e formato)
    D3D11_TEXTURE2D_DESC copy_desc = {};
    if (g_copy != nullptr)
        g_copy->GetDesc(&copy_desc);
    if (g_copy == nullptr || copy_desc.Width != desc.Width || copy_desc.Height != desc.Height || copy_desc.Format != desc.Format)
    {
        g_copy.Reset();
        g_copy_srv.Reset();
        copy_desc = desc;
        copy_desc.MipLevels = 1;
        copy_desc.ArraySize = 1;
        copy_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        copy_desc.MiscFlags = 0;
        copy_desc.CPUAccessFlags = 0;
        copy_desc.Usage = D3D11_USAGE_DEFAULT;
        D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Format = view_format(desc.Format);
        srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;
        if (desc.MipLevels != 1 || desc.ArraySize != 1 ||
            FAILED(device->CreateTexture2D(&copy_desc, nullptr, &g_copy)) ||
            FAILED(device->CreateShaderResourceView(g_copy.Get(), &srv, &g_copy_srv)))
        {
            g_copy.Reset();
            return false;
        }
    }

    auto &rtv = g_rtvs[texture];
    if (rtv == nullptr)
    {
        if (g_rtvs.size() > 16)
        {
            g_rtvs.clear(); // texture ricreate (cambio pista): si ripulisce la cache
            return false;
        }
        D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
        rtv_desc.Format = view_format(desc.Format);
        rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        if (FAILED(device->CreateRenderTargetView(texture, &rtv_desc, &g_rtvs[texture])))
        {
            g_rtvs.erase(texture);
            return false;
        }
    }

    Constants c = {};
    c.tm[0] = p.tonemap_intensity;
    c.tm[1] = p.tonemap_mode;
    c.tm[2] = p.exposure_ev;
    c.tm[3] = p.hdr_recovery;
    c.tm2[0] = p.auto_exposure;
    c.tm2[1] = adapt != nullptr ? 1.0f : 0.0f;
    c.gr[0] = p.grade_intensity;
    c.gr[1] = p.temperature;
    c.gr[2] = p.tint;
    c.gr[3] = p.vibrance;
    c.gr2[0] = p.saturation;
    c.gr2[1] = p.contrast;
    c.gr2[2] = p.lut_index;
    c.gr2[3] = p.lut_amount;
    c.misc[0] = lut != nullptr ? 1.0f : 0.0f;

    // Lo stato di rF2 viene messo da parte e ripristinato identico
    ID3DDeviceContextState *previous = nullptr;
    context1->SwapDeviceContextState(g_state.Get(), &previous);

    context1->CopyResource(g_copy.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(context1->Map(g_constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        std::memcpy(mapped.pData, &c, sizeof(c));
        context1->Unmap(g_constants.Get(), 0);
    }
    ID3D11RenderTargetView *target = g_rtvs[texture].Get();
    ID3D11ShaderResourceView *srvs[3] = {g_copy_srv.Get(), lut, adapt};
    ID3D11Buffer *cb = g_constants.Get();
    ID3D11SamplerState *sampler = g_sampler.Get();
    const D3D11_VIEWPORT viewport = {0.0f, 0.0f, static_cast<float>(desc.Width), static_cast<float>(desc.Height), 0.0f, 1.0f};
    context1->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context1->IASetInputLayout(nullptr);
    context1->VSSetShader(g_vs.Get(), nullptr, 0);
    context1->PSSetShader(g_ps.Get(), nullptr, 0);
    context1->PSSetConstantBuffers(0, 1, &cb);
    context1->PSSetSamplers(0, 1, &sampler);
    context1->PSSetShaderResources(0, 3, srvs);
    context1->OMSetRenderTargets(1, &target, nullptr);
    context1->RSSetViewports(1, &viewport);
    context1->Draw(3, 0);
    ID3D11ShaderResourceView *none[3] = {};
    context1->PSSetShaderResources(0, 3, none);
    ID3D11RenderTargetView *no_target = nullptr;
    context1->OMSetRenderTargets(1, &no_target, nullptr);

    context1->SwapDeviceContextState(previous, nullptr);
    if (previous != nullptr)
        previous->Release();
    return true;
}

void release()
{
    g_rtvs.clear();
    g_copy_srv.Reset();
    g_copy.Reset();
    g_state.Reset();
    g_sampler.Reset();
    g_constants.Reset();
    g_ps.Reset();
    g_vs.Reset();
    g_failed = false;
}
}
