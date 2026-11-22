#include "input/wheel.hpp"
#include "core/log.hpp"

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <wrl/client.h>

#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace ktv::wheel
{
namespace
{
using Microsoft::WRL::ComPtr;

struct Device
{
    GUID instance = {};
    std::string name;
    ComPtr<IDirectInputDevice8W> device;
    BYTE current[128] = {};
    BYTE previous[128] = {};
};

struct Found
{
    GUID instance = {};
    std::string name;
};

ComPtr<IDirectInput8W> g_input;
std::vector<Device> g_devices;
std::vector<GUID> g_failed_devices;
HWND g_window = nullptr;
bool g_failed = false;

// Ricerca dei controller su un thread a parte: EnumDevices di DirectInput interroga tutti
// i dispositivi USB e blocca per 100-200 ms. Fatta sul thread di rendering ogni 10 secondi
// causava uno scatto regolare in pista. Ora la ricerca si fa all'avvio e poi solo se serve
// (un pulsante assegnato a un controller non collegato, o l'assegnazione di un pulsante).
std::mutex g_scan_mutex;
std::condition_variable g_scan_wake;
std::thread *g_scan_thread = nullptr; // puntatore: a fine processo non serve distruggerlo
bool g_scan_requested = true; // la prima ricerca all'avvio
bool g_scan_stop = false;
bool g_scan_ready = false;    // risultato nuovo da elaborare sul thread di rendering
std::vector<Found> g_scan_result;
double g_last_request = -100.0;

std::string to_utf8(const wchar_t *text)
{
    char utf8[260] = {};
    WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof(utf8), nullptr, nullptr);
    return utf8;
}

double now()
{
    return static_cast<double>(GetTickCount64()) / 1000.0;
}

BOOL CALLBACK on_found(const DIDEVICEINSTANCEW *instance, void *context)
{
    auto *list = static_cast<std::vector<Found> *>(context);
    list->push_back({instance->guidInstance, to_utf8(instance->tszProductName)});
    return DIENUM_CONTINUE;
}

void scan_thread_main()
{
    ComPtr<IDirectInput8W> input;
    if (FAILED(DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W,
                                  reinterpret_cast<void **>(input.GetAddressOf()), nullptr)))
        return;
    std::unique_lock<std::mutex> lock(g_scan_mutex);
    while (!g_scan_stop)
    {
        g_scan_wake.wait(lock, [] { return g_scan_stop || g_scan_requested; });
        if (g_scan_stop)
            break;
        g_scan_requested = false;
        lock.unlock();
        std::vector<Found> found;
        input->EnumDevices(DI8DEVCLASS_GAMECTRL, &on_found, &found, DIEDFL_ATTACHEDONLY);
        lock.lock();
        g_scan_result = std::move(found);
        g_scan_ready = true;
    }
}

// Al massimo una ricerca ogni 10 secondi
void request_scan()
{
    const double t = now();
    if (t - g_last_request < 10.0)
        return;
    g_last_request = t;
    {
        const std::lock_guard<std::mutex> lock(g_scan_mutex);
        g_scan_requested = true;
    }
    g_scan_wake.notify_one();
}

void open_device(const Found &found)
{
    for (const Device &d : g_devices)
        if (IsEqualGUID(d.instance, found.instance))
            return;
    // Dispositivi che non si lasciano aprire: non si ritentano (ogni tentativo finisce nel log)
    for (const GUID &failed : g_failed_devices)
        if (IsEqualGUID(failed, found.instance))
            return;

    Device d;
    d.instance = found.instance;
    d.name = found.name;
    if (FAILED(g_input->CreateDevice(found.instance, &d.device, nullptr)) ||
        FAILED(d.device->SetDataFormat(&c_dfDIJoystick2)) ||
        FAILED(d.device->SetCooperativeLevel(g_window, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND)))
    {
        g_failed_devices.push_back(found.instance);
        return; // dispositivo non utilizzabile
    }
    d.device->Acquire();
    log::info("Controller per i tasti KarmaTV: " + d.name);
    g_devices.push_back(std::move(d));
}

// Sul thread di rendering: apre solo i controller nuovi trovati dalla ricerca
void apply_scan_result()
{
    std::vector<Found> found;
    {
        const std::lock_guard<std::mutex> lock(g_scan_mutex);
        if (!g_scan_ready)
            return;
        g_scan_ready = false;
        found = g_scan_result;
    }
    if (g_input == nullptr)
    {
        if (FAILED(DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W,
                                      reinterpret_cast<void **>(g_input.GetAddressOf()), nullptr)))
        {
            g_failed = true;
            log::warning("DirectInput non disponibile: pulsanti del volante disattivati.");
            return;
        }
    }
    // Controller scollegati: tolti dall'elenco (verranno riaperti quando tornano)
    for (size_t i = 0; i < g_devices.size();)
    {
        bool present = false;
        for (const Found &f : found)
            present = present || IsEqualGUID(f.instance, g_devices[i].instance);
        if (present)
            ++i;
        else
            g_devices.erase(g_devices.begin() + static_cast<std::ptrdiff_t>(i));
    }
    for (const Found &f : found)
        open_device(f);
}

const Device *find(const GUID &instance)
{
    for (const Device &d : g_devices)
        if (IsEqualGUID(d.instance, instance))
            return &d;
    return nullptr;
}

bool edge(const Device &d, int button)
{
    return button >= 0 && button < 128 && (d.current[button] & 0x80) != 0 && (d.previous[button] & 0x80) == 0;
}
}

void poll(HWND window)
{
    if (g_failed || window == nullptr)
        return;
    if (g_scan_thread == nullptr)
        g_scan_thread = new std::thread(scan_thread_main);
    if (g_window != window)
    {
        g_window = window;
        g_devices.clear(); // il livello di cooperazione dipende dalla finestra
        const std::lock_guard<std::mutex> lock(g_scan_mutex);
        if (!g_scan_result.empty())
            g_scan_ready = true; // si riaprono con la nuova finestra, senza rifare la ricerca
    }
    apply_scan_result();

    for (Device &d : g_devices)
    {
        std::memcpy(d.previous, d.current, sizeof(d.current));
        DIJOYSTATE2 state = {};
        d.device->Poll();
        HRESULT hr = d.device->GetDeviceState(sizeof(state), &state);
        if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED)
        {
            d.device->Acquire();
            hr = d.device->GetDeviceState(sizeof(state), &state);
        }
        if (SUCCEEDED(hr))
            std::memcpy(d.current, state.rgbButtons, sizeof(d.current));
        else
            std::memset(d.current, 0, sizeof(d.current));
    }
}

void shutdown()
{
    {
        const std::lock_guard<std::mutex> lock(g_scan_mutex);
        g_scan_stop = true;
    }
    g_scan_wake.notify_one();
    if (g_scan_thread != nullptr)
    {
        g_scan_thread->join();
        delete g_scan_thread;
        g_scan_thread = nullptr;
    }
    g_scan_stop = false;
    g_scan_requested = true;
    g_scan_ready = false;
    g_scan_result.clear();
    for (Device &d : g_devices)
        if (d.device != nullptr)
            d.device->Unacquire();
    g_devices.clear();
    g_input.Reset();
}

bool pressed(const Binding &binding)
{
    if (!binding.valid())
        return false;
    const Device *d = find(binding.device);
    if (d == nullptr)
    {
        request_scan(); // controller assegnato ma non collegato: lo si cerca (ogni 10 s)
        return false;
    }
    return edge(*d, binding.button);
}

bool capture(Binding &out)
{
    request_scan(); // durante l'assegnazione si cercano anche i controller appena collegati
    for (const Device &d : g_devices)
    {
        for (int b = 0; b < 128; ++b)
        {
            if (!edge(d, b))
                continue;
            out.device = d.instance;
            out.button = b;
            out.device_name = d.name;
            return true;
        }
    }
    return false;
}

std::string describe(const Binding &binding)
{
    if (!binding.valid())
        return "nessuno";
    const Device *d = find(binding.device);
    const std::string name = d != nullptr ? d->name : (binding.device_name.empty() ? "controller non collegato" : binding.device_name);
    return name + ", pulsante " + std::to_string(binding.button + 1);
}

std::string to_string(const Binding &binding)
{
    if (!binding.valid())
        return "";
    wchar_t guid[64] = {};
    StringFromGUID2(binding.device, guid, 64);
    return to_utf8(guid) + "," + std::to_string(binding.button);
}

Binding from_string(const std::string &text)
{
    Binding binding;
    const size_t comma = text.find(',');
    if (comma == std::string::npos)
        return binding;
    wchar_t guid[64] = {};
    MultiByteToWideChar(CP_UTF8, 0, text.substr(0, comma).c_str(), -1, guid, 64);
    if (FAILED(CLSIDFromString(guid, &binding.device)))
        return Binding{};
    try { binding.button = std::stoi(text.substr(comma + 1)); }
    catch (...) { return Binding{}; }
    return binding;
}

int device_count()
{
    return static_cast<int>(g_devices.size());
}
}
