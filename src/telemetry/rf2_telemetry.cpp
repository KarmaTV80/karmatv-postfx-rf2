#include "common.hpp"
#include "telemetry/rf2_telemetry.hpp"
#include "telemetry/rf2_layout.hpp"
#include "core/log.hpp"

#include <cmath>
#include <cstring>

namespace ktv
{
namespace
{
std::string from_fixed(const char *text, size_t capacity)
{
    return std::string(text, strnlen(text, capacity));
}

bool plausible(const rf2::ScoringInfo &info)
{
    return info.mNumVehicles >= 0 && info.mNumVehicles <= rf2::kMaxMappedVehicles &&
           info.mRaining >= 0.0 && info.mRaining <= 1.0 &&
           info.mDarkCloud >= 0.0 && info.mDarkCloud <= 1.0;
}
}

Rf2Telemetry::Rf2Telemetry() : scratch_(std::make_unique<rf2::ScoringBuffer>()) {}

Rf2Telemetry::~Rf2Telemetry()
{
    close();
}

void Rf2Telemetry::close()
{
    if (graphics_view_ != nullptr)
        UnmapViewOfFile(graphics_view_);
    if (graphics_mapping_ != nullptr)
        CloseHandle(static_cast<HANDLE>(graphics_mapping_));
    graphics_view_ = nullptr;
    graphics_mapping_ = nullptr;
    if (view_ != nullptr)
        UnmapViewOfFile(view_);
    if (mapping_ != nullptr)
        CloseHandle(static_cast<HANDLE>(mapping_));
    view_ = nullptr;
    mapping_ = nullptr;
}

void Rf2Telemetry::poll()
{
    if (view_ == nullptr)
    {
        HANDLE mapping = OpenFileMappingA(FILE_MAP_READ, FALSE, rf2::kScoringMappingName);
        if (mapping == nullptr)
        {
            snapshot_.connected = false;
            return;
        }
        const void *view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(rf2::ScoringBuffer));
        if (view == nullptr)
        {
            // Buffer piu' piccolo del previsto: layout diverso da quello atteso.
            CloseHandle(mapping);
            snapshot_.connected = false;
            return;
        }
        mapping_ = mapping;
        view_ = view;
        log::info("Shared memory di rF2 collegata.");
    }

    // Copia coerente: il plugin incrementa begin prima di scrivere ed end dopo.
    const auto *source = static_cast<const rf2::ScoringBuffer *>(view_);
    bool consistent = false;
    for (int attempt = 0; attempt < 4 && !consistent; ++attempt)
    {
        const unsigned long begin_before = *reinterpret_cast<const volatile unsigned long *>(&source->mVersionUpdateBegin);
        std::memcpy(scratch_.get(), source, sizeof(rf2::ScoringBuffer));
        consistent = scratch_->mVersionUpdateBegin == scratch_->mVersionUpdateEnd &&
                     scratch_->mVersionUpdateBegin == begin_before;
    }

    // Lettura capitata mentre il plugin scriveva (succede spesso nei replay, dove i dati
    // cambiano di continuo): si tiene l'ultima lettura valida invece di dichiarare la
    // telemetria persa per un frame. Persa davvero solo dopo ~60 letture fallite di fila.
    static int bad_reads = 0;
    const rf2::ScoringInfo &info = scratch_->mScoringInfo;
    if (!consistent || !plausible(info))
    {
        if (++bad_reads >= 60)
            snapshot_.connected = false;
        return;
    }
    bad_reads = 0;

    TelemetrySnapshot s;
    s.connected = true;
    s.in_realtime = info.mInRealtime;
    s.track_name = from_fixed(info.mTrackName, sizeof(info.mTrackName));
    s.raining = info.mRaining;
    s.dark_cloud = info.mDarkCloud;
    s.wetness_avg = info.mAvgPathWetness;
    s.track_temp = info.mTrackTemp;
    // Ora del giorno: inizio evento (secondi dalla mezzanotte) + tempo di sessione.
    // Con l'accelerazione del tempo attiva in rF2 il valore resta un'approssimazione.
    s.time_of_day = std::fmod(static_cast<double>(info.mStartET) + info.mCurrentET, 86400.0);
    if (s.time_of_day < 0.0)
        s.time_of_day += 86400.0;
    s.yellow_flag_state = info.mYellowFlagState;
    s.session = info.mSession;
    s.game_phase = info.mGamePhase;

    for (long i = 0; i < info.mNumVehicles; ++i)
    {
        const rf2::VehicleScoring &vehicle = scratch_->mVehicles[i];
        if (!vehicle.mIsPlayer)
            continue;
        s.vehicle_name = from_fixed(vehicle.mVehicleName, sizeof(vehicle.mVehicleName));
        s.in_pits = vehicle.mInPits;
        s.player_flag = vehicle.mFlag;
        break;
    }

    s.camera_type = snapshot_.camera_type;
    snapshot_ = std::move(s);
}

void Rf2Telemetry::poll_camera()
{
    if (graphics_view_ == nullptr)
    {
        // Buffer non pubblicato (UnsubscribedBuffersMask): si riprova ogni 5 secondi
        const double t = static_cast<double>(GetTickCount64()) / 1000.0;
        if (t < graphics_retry_)
            return;
        graphics_retry_ = t + 5.0;
        HANDLE mapping = OpenFileMappingA(FILE_MAP_READ, FALSE, rf2::kGraphicsMappingName);
        if (mapping == nullptr)
            return;
        const void *view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(rf2::GraphicsBuffer));
        if (view == nullptr)
        {
            CloseHandle(mapping);
            return;
        }
        graphics_mapping_ = mapping;
        graphics_view_ = view;
        log::info("Telecamera di rF2: buffer Graphics collegato.");
    }
    const auto *source = static_cast<const rf2::GraphicsBuffer *>(graphics_view_);
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const unsigned long begin = *reinterpret_cast<const volatile unsigned long *>(&source->mVersionUpdateBegin);
        const long type = *reinterpret_cast<const volatile long *>(&source->mGraphicsInfo.mCameraType);
        const unsigned long end = *reinterpret_cast<const volatile unsigned long *>(&source->mVersionUpdateEnd);
        if (begin != end || begin == 0)
            continue; // in scrittura, o buffer mai aggiornato (plugin senza Graphics)
        // Controllo del layout: la luce ambiente e' un colore 0..qualche unita'
        const double ambient = source->mGraphicsInfo.mAmbientRed;
        const bool layout_ok = ambient >= 0.0 && ambient < 50.0;
        const int value = layout_ok && type >= 0 && type <= 2000 ? static_cast<int>(type) : -1;
        if (value != snapshot_.camera_type)
        {
            static int logged = 0;
            if (logged < 40)
            {
                ++logged;
                log::info("Telecamera di rF2: tipo " + std::to_string(value));
            }
        }
        snapshot_.camera_type = value;
        return;
    }
}
}
