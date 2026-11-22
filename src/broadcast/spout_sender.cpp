#include "broadcast/spout_sender.hpp"
#include "core/log.hpp"

#include <dxgi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

namespace ktv::spout
{
namespace
{
using Microsoft::WRL::ComPtr;

constexpr size_t kNameLength = 256; // SpoutMaxSenderNameLen
constexpr size_t kMaxSenders = 64;  // valore predefinito di Spout 2.007
constexpr DWORD kLockTimeoutMs = 67;

#pragma pack(push, 4)
// Stessa disposizione della struttura SharedTextureInfo di Spout (280 byte)
struct SharedTextureInfo
{
    uint32_t share_handle;
    uint32_t width;
    uint32_t height;
    uint32_t format; // DXGI_FORMAT
    uint32_t usage;
    wchar_t description[128];
    uint32_t partner_id;
};
#pragma pack(pop)
static_assert(sizeof(SharedTextureInfo) == 280, "SharedTextureInfo deve restare di 280 byte");

// Memoria condivisa con il suo mutex "<nome>_mutex", come SpoutSharedMemory
struct SharedMemory
{
    HANDLE mapping = nullptr;
    HANDLE mutex = nullptr;
    void *view = nullptr;
    size_t size = 0;

    bool create(const std::string &name, size_t bytes)
    {
        mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, static_cast<DWORD>(bytes), name.c_str());
        if (mapping == nullptr)
            return false;
        view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        if (view == nullptr)
        {
            close();
            return false;
        }
        // Se la memoria esisteva gia' (creata da un altro programma Spout) la dimensione
        // reale puo' essere diversa da quella chiesta: si legge dalla vista.
        MEMORY_BASIC_INFORMATION info = {};
        size = VirtualQuery(view, &info, sizeof(info)) != 0 ? std::min(bytes, static_cast<size_t>(info.RegionSize)) : bytes;
        mutex = CreateMutexA(nullptr, FALSE, (name + "_mutex").c_str());
        return true;
    }

    bool lock() const
    {
        if (mutex == nullptr)
            return true;
        const DWORD result = WaitForSingleObject(mutex, kLockTimeoutMs);
        return result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
    }

    void unlock() const
    {
        if (mutex != nullptr)
            ReleaseMutex(mutex);
    }

    void close()
    {
        if (view != nullptr)
            UnmapViewOfFile(view);
        if (mapping != nullptr)
            CloseHandle(mapping);
        if (mutex != nullptr)
            CloseHandle(mutex);
        view = nullptr;
        mapping = nullptr;
        mutex = nullptr;
        size = 0;
    }
};

SharedMemory g_names;       // "SpoutSenderNames"
SharedMemory g_active_name; // "ActiveSenderName"
SharedMemory g_info;        // "<nome del sender>"
HANDLE g_access_mutex = nullptr;

ComPtr<ID3D11Texture2D> g_texture;
HANDLE g_share_handle = nullptr;
D3D11_TEXTURE2D_DESC g_desc = {};
bool g_registered = false;
bool g_failed = false;
unsigned long long g_frames = 0;

DXGI_FORMAT copy_format(DXGI_FORMAT format)
{
    // Stesso gruppo di formati del back buffer (CopyResource), senza la variante sRGB
    switch (format)
    {
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    default: return format;
    }
}

bool register_name()
{
    if (!g_names.create("SpoutSenderNames", kNameLength * kMaxSenders))
        return false;
    if (!g_names.lock())
        return false;
    char *slots = static_cast<char *>(g_names.view);
    const size_t count = g_names.size / kNameLength;
    bool present = false;
    size_t free_slot = count;
    for (size_t i = 0; i < count; ++i)
    {
        const char *slot = slots + i * kNameLength;
        if (slot[0] == '\0')
        {
            free_slot = i; // l'elenco finisce al primo blocco vuoto
            break;
        }
        if (std::strncmp(slot, kSenderName, kNameLength) == 0)
            present = true;
    }
    const bool ok = present || free_slot < count;
    if (!present && ok)
    {
        std::memset(slots + free_slot * kNameLength, 0, kNameLength);
        std::memcpy(slots + free_slot * kNameLength, kSenderName, std::strlen(kSenderName));
    }
    g_names.unlock();

    // Sender "attivo" se nessun altro lo e' gia': i ricevitori senza nome si collegano qui
    if (g_active_name.create("ActiveSenderName", kNameLength) && g_active_name.lock())
    {
        char *active = static_cast<char *>(g_active_name.view);
        if (active[0] == '\0')
            std::memcpy(active, kSenderName, std::strlen(kSenderName) + 1);
        g_active_name.unlock();
    }
    return ok;
}

void unregister_name()
{
    if (g_names.view != nullptr && g_names.lock())
    {
        // Si toglie il nome e si compattano i successivi (l'elenco non deve avere buchi)
        char *slots = static_cast<char *>(g_names.view);
        const size_t count = g_names.size / kNameLength;
        size_t write = 0;
        for (size_t read = 0; read < count; ++read)
        {
            char *slot = slots + read * kNameLength;
            if (slot[0] == '\0')
                break;
            if (std::strncmp(slot, kSenderName, kNameLength) == 0)
                continue;
            if (write != read)
                std::memcpy(slots + write * kNameLength, slot, kNameLength);
            ++write;
        }
        for (size_t i = write; i < count && slots[i * kNameLength] != '\0'; ++i)
            std::memset(slots + i * kNameLength, 0, kNameLength);
        g_names.unlock();
    }
    if (g_active_name.view != nullptr && g_active_name.lock())
    {
        char *active = static_cast<char *>(g_active_name.view);
        if (std::strncmp(active, kSenderName, kNameLength) == 0)
            std::memset(active, 0, kNameLength);
        g_active_name.unlock();
    }
}

void write_info()
{
    if (g_info.view == nullptr || !g_info.lock())
        return;
    SharedTextureInfo info = {};
    // Gli handle condivisi "legacy" stanno in 32 bit anche nei processi a 64 bit
    info.share_handle = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_share_handle));
    info.width = g_desc.Width;
    info.height = g_desc.Height;
    info.format = static_cast<uint32_t>(g_desc.Format);
    info.usage = 0;
    std::memcpy(g_info.view, &info, sizeof(info));
    g_info.unlock();
}

bool create_texture(ID3D11Device *device, const D3D11_TEXTURE2D_DESC &source)
{
    g_texture.Reset();
    g_share_handle = nullptr;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = source.Width;
    desc.Height = source.Height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = copy_format(source.Format);
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &g_texture)))
        return false;

    ComPtr<IDXGIResource> resource;
    if (FAILED(g_texture.As(&resource)) || FAILED(resource->GetSharedHandle(&g_share_handle)) || g_share_handle == nullptr)
    {
        g_texture.Reset();
        return false;
    }
    g_desc = desc;
    return true;
}
}

void publish(ID3D11DeviceContext *context, ID3D11Resource *source)
{
    if (g_failed || context == nullptr || source == nullptr)
        return;

    ComPtr<ID3D11Texture2D> source_texture;
    if (FAILED(source->QueryInterface(IID_PPV_ARGS(&source_texture))))
        return;
    D3D11_TEXTURE2D_DESC source_desc = {};
    source_texture->GetDesc(&source_desc);
    if (source_desc.SampleDesc.Count > 1)
        return; // il back buffer di rF2 non e' multicampionato; per sicurezza

    const bool resized = g_texture == nullptr || source_desc.Width != g_desc.Width || source_desc.Height != g_desc.Height ||
                         copy_format(source_desc.Format) != g_desc.Format;
    if (resized)
    {
        ComPtr<ID3D11Device> device;
        context->GetDevice(&device);
        if (!create_texture(device.Get(), source_desc))
        {
            g_failed = true;
            log::warning("Clean feed Spout2: impossibile creare la texture condivisa.");
            return;
        }
        if (!g_registered)
        {
            if (!g_info.create(kSenderName, sizeof(SharedTextureInfo)) || !register_name())
            {
                g_failed = true;
                log::warning("Clean feed Spout2: impossibile registrare il sender.");
                shutdown();
                return;
            }
            g_access_mutex = CreateMutexA(nullptr, FALSE, (std::string(kSenderName) + "_SpoutAccessMutex").c_str());
            g_registered = true;
        }
        write_info();
        log::info("Clean feed Spout2 \"" + std::string(kSenderName) + "\": " + std::to_string(g_desc.Width) + "x" +
                  std::to_string(g_desc.Height) + ", formato DXGI " + std::to_string(g_desc.Format) + ".");
    }

    // Se un ricevitore sta leggendo la texture si salta il frame invece di aspettare
    if (g_access_mutex != nullptr)
    {
        const DWORD result = WaitForSingleObject(g_access_mutex, 0);
        if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED)
            return;
    }
    context->CopyResource(g_texture.Get(), source);
    // Il ricevitore usa un altro device: i comandi vanno inviati alla GPU subito
    context->Flush();
    if (g_access_mutex != nullptr)
        ReleaseMutex(g_access_mutex);
    ++g_frames;
}

void shutdown()
{
    if (g_registered)
    {
        unregister_name();
        log::info("Clean feed Spout2 chiuso.");
    }
    g_registered = false;
    g_info.close();
    g_names.close();
    g_active_name.close();
    if (g_access_mutex != nullptr)
        CloseHandle(g_access_mutex);
    g_access_mutex = nullptr;
    g_texture.Reset();
    g_share_handle = nullptr;
    g_desc = {};
}

bool active()
{
    return g_registered;
}

unsigned width()
{
    return g_desc.Width;
}

unsigned height()
{
    return g_desc.Height;
}

unsigned long long frames()
{
    return g_frames;
}
}
