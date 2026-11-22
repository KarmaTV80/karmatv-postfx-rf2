#pragma once

#include <cstdint>
#include <unordered_map>

#include <d3d11.h>
#include <wrl/client.h>

namespace ktv
{
// Misura il tempo GPU degli effetti con query timestamp D3D11.
// Un timestamp all'inizio degli effetti e uno alla FINE di ogni technique (ReShade
// segnala reshade_render_technique dopo averla eseguita): il tempo di una technique
// e' la distanza dal timestamp precedente.
// I risultati arrivano con qualche frame di ritardo (anello di 4 frame).
class GpuProfiler
{
public:
    void init(ID3D11Device *device);
    void release();
    bool available() const { return device_ != nullptr; }

    void begin(ID3D11DeviceContext *context);
    void mark_technique(ID3D11DeviceContext *context, uint64_t technique);
    void end(ID3D11DeviceContext *context);
    void collect(ID3D11DeviceContext *context);

    float total_ms() const { return total_ms_; }
    float technique_ms(uint64_t technique) const;

private:
    static constexpr int kFrames = 4;
    static constexpr int kMaxMarks = 48;

    struct Frame
    {
        Microsoft::WRL::ComPtr<ID3D11Query> disjoint;
        Microsoft::WRL::ComPtr<ID3D11Query> stamps[kMaxMarks + 2];
        uint64_t techniques[kMaxMarks] = {};
        int mark_count = 0;
        bool pending = false;
    };

    ID3D11Device *device_ = nullptr;
    Frame frames_[kFrames];
    int current_ = 0;
    bool recording_ = false;
    float total_ms_ = 0.0f;
    struct Timing
    {
        float ms = 0.0f;
        uint64_t last_seen = 0; // numero della misura in cui la technique e' stata eseguita
    };
    std::unordered_map<uint64_t, Timing> technique_ms_;
    uint64_t collected_ = 0;
};
}
