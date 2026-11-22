#include "perf/gpu_profiler.hpp"
#include "core/log.hpp"
#include "perf/spike_monitor.hpp"

#include <utility>
#include <vector>

namespace ktv
{
namespace
{
constexpr float kSmoothing = 0.1f; // media mobile esponenziale: valori leggibili nel menu

float smooth(float previous, float sample)
{
    return previous <= 0.0f ? sample : previous + (sample - previous) * kSmoothing;
}
}

void GpuProfiler::init(ID3D11Device *device)
{
    if (device_ != nullptr || device == nullptr)
        return;

    D3D11_QUERY_DESC disjoint_desc = {D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
    D3D11_QUERY_DESC stamp_desc = {D3D11_QUERY_TIMESTAMP, 0};

    for (Frame &frame : frames_)
    {
        if (FAILED(device->CreateQuery(&disjoint_desc, &frame.disjoint)))
        {
            log::warning("Profiler GPU non disponibile: creazione delle query fallita.");
            release();
            return;
        }
        for (auto &stamp : frame.stamps)
        {
            if (FAILED(device->CreateQuery(&stamp_desc, &stamp)))
            {
                log::warning("Profiler GPU non disponibile: creazione delle query fallita.");
                release();
                return;
            }
        }
    }
    device_ = device;
}

void GpuProfiler::release()
{
    for (Frame &frame : frames_)
    {
        frame.disjoint.Reset();
        for (auto &stamp : frame.stamps)
            stamp.Reset();
        frame.pending = false;
        frame.mark_count = 0;
    }
    device_ = nullptr;
    recording_ = false;
    total_ms_ = 0.0f;
    technique_ms_.clear();
    collected_ = 0;
}

void GpuProfiler::begin(ID3D11DeviceContext *context)
{
    if (device_ == nullptr || context == nullptr || recording_)
        return;
    Frame &frame = frames_[current_];
    if (frame.pending)
        return; // risultati vecchi non ancora letti: questo frame non si misura

    frame.mark_count = 0;
    context->Begin(frame.disjoint.Get());
    context->End(frame.stamps[0].Get());
    recording_ = true;
}

void GpuProfiler::mark_technique(ID3D11DeviceContext *context, uint64_t technique)
{
    if (!recording_ || context == nullptr)
        return;
    Frame &frame = frames_[current_];
    if (frame.mark_count >= kMaxMarks)
        return;
    frame.techniques[frame.mark_count] = technique;
    context->End(frame.stamps[1 + frame.mark_count].Get());
    ++frame.mark_count;
}

void GpuProfiler::end(ID3D11DeviceContext *context)
{
    if (!recording_ || context == nullptr)
        return;
    Frame &frame = frames_[current_];
    context->End(frame.stamps[1 + frame.mark_count].Get());
    context->End(frame.disjoint.Get());
    frame.pending = true;
    recording_ = false;
    current_ = (current_ + 1) % kFrames;
}

void GpuProfiler::collect(ID3D11DeviceContext *context)
{
    if (device_ == nullptr || context == nullptr)
        return;

    for (Frame &frame : frames_)
    {
        if (!frame.pending)
            continue;

        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint = {};
        if (context->GetData(frame.disjoint.Get(), &disjoint, sizeof(disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
            continue; // non ancora pronto

        frame.pending = false;
        if (disjoint.Disjoint || disjoint.Frequency == 0)
            continue;

        UINT64 stamps[kMaxMarks + 2] = {};
        bool complete = true;
        for (int i = 0; i < frame.mark_count + 2 && complete; ++i)
            complete = context->GetData(frame.stamps[i].Get(), &stamps[i], sizeof(UINT64), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
        if (!complete)
            continue;

        const double to_ms = 1000.0 / static_cast<double>(disjoint.Frequency);
        const int last = frame.mark_count + 1;
        total_ms_ = smooth(total_ms_, static_cast<float>((stamps[last] - stamps[0]) * to_ms));
        ++collected_;

        // stamps[0] = inizio, stamps[1 + i] = fine della technique i
        std::vector<std::pair<uint64_t, float>> raw;
        raw.reserve(static_cast<size_t>(frame.mark_count));
        for (int i = 0; i < frame.mark_count; ++i)
        {
            const float ms = static_cast<float>((stamps[i + 1] - stamps[i]) * to_ms);
            raw.emplace_back(frame.techniques[i], ms);
            Timing &timing = technique_ms_[frame.techniques[i]];
            // Riaccesa dopo una pausa: si riparte dalla misura nuova, non dalla media vecchia
            timing.ms = collected_ - timing.last_seen > 30 ? ms : smooth(timing.ms, ms);
            timing.last_seen = collected_;
        }
        spikes::gpu_frame(static_cast<float>((stamps[last] - stamps[0]) * to_ms), raw);
    }
}

float GpuProfiler::technique_ms(uint64_t technique) const
{
    const auto it = technique_ms_.find(technique);
    // Non eseguita nelle ultime ~30 misure (spenta): nessun costo
    if (it == technique_ms_.end() || collected_ - it->second.last_seen > 30)
        return 0.0f;
    return it->second.ms;
}
}
