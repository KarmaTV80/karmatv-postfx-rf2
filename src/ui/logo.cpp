#include "ui/logo.hpp"
#include "app.hpp"
#include "core/log.hpp"

#include <wincodec.h>
#include <wrl/client.h>

#include <vector>

namespace ktv::logo
{
namespace
{
using Microsoft::WRL::ComPtr;
using namespace reshade::api;

constexpr int kResourceId = 101;

device *g_device = nullptr;
resource g_texture = {0};
resource_view g_view = {0};
float g_aspect = 0.0f;
bool g_tried = false;

bool decode(std::vector<uint8_t> &pixels, UINT &width, UINT &height)
{
    HMODULE module = app().module;
    HRSRC info = FindResourceW(module, MAKEINTRESOURCEW(kResourceId), RT_RCDATA);
    if (info == nullptr)
        return false;
    HGLOBAL handle = LoadResource(module, info);
    const void *data = handle != nullptr ? LockResource(handle) : nullptr;
    const DWORD size = SizeofResource(module, info);
    if (data == nullptr || size == 0)
        return false;

    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool ok = false;
    {
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICFormatConverter> converter;
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
            SUCCEEDED(factory->CreateStream(&stream)) &&
            SUCCEEDED(stream->InitializeFromMemory(static_cast<BYTE *>(const_cast<void *>(data)), size)) &&
            SUCCEEDED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) &&
            SUCCEEDED(decoder->GetFrame(0, &frame)) &&
            SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
            SUCCEEDED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)) &&
            SUCCEEDED(converter->GetSize(&width, &height)) && width > 0 && height > 0)
        {
            pixels.resize(static_cast<size_t>(width) * height * 4);
            ok = SUCCEEDED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data()));
        }
    }
    if (init == S_OK || init == S_FALSE)
        CoUninitialize();
    return ok;
}

bool load(effect_runtime *runtime)
{
    if (g_view.handle != 0)
        return true;
    if (g_tried)
        return false;
    g_tried = true;

    std::vector<uint8_t> pixels;
    UINT width = 0, height = 0;
    if (!decode(pixels, width, height))
    {
        log::warning("Logo KarmaTV non caricato (decodifica PNG non riuscita).");
        return false;
    }
    device *dev = runtime->get_device();
    const resource_desc desc(width, height, 1, 1, format::r8g8b8a8_unorm, 1, memory_heap::default_, resource_usage::shader_resource);
    subresource_data initial;
    initial.data = pixels.data();
    initial.row_pitch = width * 4;
    initial.slice_pitch = width * height * 4;
    if (!dev->create_resource(desc, &initial, resource_usage::shader_resource, &g_texture))
        return false;
    if (!dev->create_resource_view(g_texture, resource_usage::shader_resource, resource_view_desc(format::r8g8b8a8_unorm), &g_view))
    {
        dev->destroy_resource(g_texture);
        g_texture = {0};
        return false;
    }
    g_device = dev;
    g_aspect = static_cast<float>(width) / static_cast<float>(height);
    return true;
}
}

bool draw(effect_runtime *runtime, float height)
{
    if (!load(runtime))
        return false;
    ImGui::Image(ImTextureRef(g_view.handle), ImVec2(height * g_aspect, height), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
    return true;
}

float width_for(float height)
{
    return height * g_aspect;
}

void release()
{
    if (g_device != nullptr)
    {
        if (g_view.handle != 0)
            g_device->destroy_resource_view(g_view);
        if (g_texture.handle != 0)
            g_device->destroy_resource(g_texture);
    }
    g_device = nullptr;
    g_view = {0};
    g_texture = {0};
    g_tried = false;
}
}
