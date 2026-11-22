#pragma once

#include <memory>
#include <string>

namespace ktv
{
namespace rf2
{
struct ScoringBuffer;
}

// Valori letti dalla shared memory e usati da profili e automatismi.
struct TelemetrySnapshot
{
    bool connected = false;   // shared memory trovata e letta in modo coerente
    bool in_realtime = false; // in pista (non nei menu del box)
    std::string track_name;
    std::string vehicle_name;
    double raining = 0.0;     // 0..1
    double dark_cloud = 0.0;  // 0..1
    double wetness_avg = 0.0; // 0..1
    double track_temp = 25.0;           // gradi Celsius
    double time_of_day = 12.0 * 3600.0; // secondi dalla mezzanotte in pista (inizio evento + tempo trascorso)
    bool in_pits = false;
    int yellow_flag_state = 0;
    int player_flag = 0;      // 0 = verde, 6 = blu
    int session = 0;
    int game_phase = 0;
    // Telecamera di rF2 (buffer Graphics del plugin): -1 = non disponibile
    int camera_type = -1;
};

class Rf2Telemetry
{
public:
    Rf2Telemetry();
    ~Rf2Telemetry();
    Rf2Telemetry(const Rf2Telemetry &) = delete;
    Rf2Telemetry &operator=(const Rf2Telemetry &) = delete;

    // Da chiamare qualche volta al secondo (non serve ad ogni frame).
    void poll();
    // Solo il tipo di telecamera: lettura minima, da fare ad ogni frame (i cambi di
    // inquadratura nel replay sono istantanei).
    void poll_camera();
    const TelemetrySnapshot &snapshot() const { return snapshot_; }

private:
    void close();

    void *mapping_ = nullptr;
    void *graphics_mapping_ = nullptr;
    const void *graphics_view_ = nullptr;
    double graphics_retry_ = 0.0;
    const void *view_ = nullptr;
    std::unique_ptr<rf2::ScoringBuffer> scratch_;
    TelemetrySnapshot snapshot_;
};
}
