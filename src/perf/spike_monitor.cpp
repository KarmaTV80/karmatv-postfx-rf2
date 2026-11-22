#include "perf/spike_monitor.hpp"
#include "app.hpp"
#include "core/effects_catalog.hpp"
#include "core/log.hpp"

#include <algorithm>
#include <cstdio>
#include <deque>
#include <mutex>

namespace ktv::spikes
{
namespace
{
struct Event
{
    double time;
    std::string text;
};

std::mutex g_mutex;
std::deque<Event> g_events;
double g_now = 0.0;
double g_last_report = -10.0;
float g_cpu_average = 0.0f;
float g_gpu_average = 0.0f;
uint64_t g_frames = 0;
int g_count = 0;

std::string recent_events()
{
    std::string text;
    for (const Event &e : g_events)
        if (g_now - e.time <= 1.0)
            text += (text.empty() ? "" : ", ") + e.text;
    return text.empty() ? "nessuno" : text;
}

std::string technique_name(uint64_t handle)
{
    App &a = app();
    if (a.primary_runtime != nullptr)
        for (const EffectInfo &e : effect_catalog())
            if (e.fx_file != nullptr && a.pipeline.technique_handle(a.primary_runtime, e.id) == handle)
                return e.technique;
    return "?";
}

bool may_report()
{
    // Una riga al secondo al massimo, 60 per sessione
    if (g_now - g_last_report < 1.0 || g_count >= 60)
        return false;
    g_last_report = g_now;
    ++g_count;
    return true;
}
}

void note(std::string text)
{
    const std::lock_guard<std::mutex> lock(g_mutex);
    g_events.push_back({g_now, std::move(text)});
    while (g_events.size() > 32 || (!g_events.empty() && g_now - g_events.front().time > 2.0))
        g_events.pop_front();
}

void cpu_frame(double now, float dt)
{
    const std::lock_guard<std::mutex> lock(g_mutex);
    g_now = now;
    ++g_frames;
    if (dt <= 0.0f || dt > 1.0f)
        return; // pause, caricamenti
    // Solo in pista o nel replay: nei menu rF2 va a scatti per conto suo e i 60 messaggi
    // della sessione finirebbero prima di salire in macchina
    {
        App &a = app();
        const TelemetrySnapshot &telemetry = a.telemetry.snapshot();
        if (telemetry.connected && !telemetry.in_realtime && !a.pipeline.replay_detected())
            return;
    }
    const float ms = dt * 1000.0f;
    if (g_cpu_average <= 0.0f)
        g_cpu_average = ms;
    // Picco: almeno il doppio della media e 8 ms in piu' (dopo i primi 10 secondi circa)
    if (g_frames > 600 && ms > g_cpu_average * 2.0f && ms > g_cpu_average + 8.0f && may_report())
    {
        char line[160];
        std::snprintf(line, sizeof(line), "Picco: frame di %.1f ms (media %.1f ms). Eventi nell'ultimo secondo: ", ms, g_cpu_average);
        log::warning(line + recent_events());
    }
    g_cpu_average += (std::min(ms, g_cpu_average * 3.0f) - g_cpu_average) * 0.02f;
}

void gpu_frame(float total_ms, const std::vector<std::pair<uint64_t, float>> &techniques)
{
    const std::lock_guard<std::mutex> lock(g_mutex);
    if (g_gpu_average <= 0.0f)
        g_gpu_average = total_ms;
    if (g_frames > 600 && total_ms > g_gpu_average * 1.6f && total_ms > g_gpu_average + 1.0f && may_report())
    {
        std::vector<std::pair<uint64_t, float>> sorted = techniques;
        std::sort(sorted.begin(), sorted.end(), [](const auto &l, const auto &r) { return l.second > r.second; });
        std::string detail;
        for (size_t i = 0; i < sorted.size() && i < 5; ++i)
        {
            char part[80];
            std::snprintf(part, sizeof(part), "%s%s %.2f ms", i == 0 ? "" : ", ", technique_name(sorted[i].first).c_str(), sorted[i].second);
            detail += part;
        }
        char line[160];
        std::snprintf(line, sizeof(line), "Picco GPU degli effetti: %.2f ms (media %.2f ms). Effetti piu' costosi: ", total_ms, g_gpu_average);
        log::warning(line + detail + ". Eventi nell'ultimo secondo: " + recent_events());
    }
    g_gpu_average += (std::min(total_ms, g_gpu_average * 3.0f) - g_gpu_average) * 0.05f;
}

int count()
{
    const std::lock_guard<std::mutex> lock(g_mutex);
    return g_count;
}
}
