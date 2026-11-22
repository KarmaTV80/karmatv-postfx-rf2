#pragma once

// Copia minima del layout della shared memory di rF2SharedMemoryMapPlugin
// (TheIronWolfModding, Include/rF2State.h), buffer "$rFactor2SMMP_Scoring$".
// Riportiamo solo le strutture che servono, campo per campo e con lo stesso
// pack(4), per non dipendere dagli header dell'Internals Plugin di ISI.
//
// Layout del buffer mappato: blocco versione (begin/end) + rF2Scoring.
// DA VERIFICARE con il plugin installato: se il layout non corrisponde, la
// lettura viene scartata dai controlli di coerenza in rf2_telemetry.cpp.

#pragma pack(push, 4)

namespace ktv::rf2
{
struct Vec3
{
    double x, y, z;
};

struct ScoringInfo
{
    char mTrackName[64];
    long mSession;
    double mCurrentET;
    double mEndET;
    long mMaxLaps;
    double mLapDist;
    unsigned char pointer1[8];
    long mNumVehicles;
    unsigned char mGamePhase;
    signed char mYellowFlagState;
    signed char mSectorFlag[3];
    unsigned char mStartLight;
    unsigned char mNumRedLights;
    bool mInRealtime;
    char mPlayerName[32];
    char mPlrFileName[64];
    double mDarkCloud;
    double mRaining;
    double mAmbientTemp;
    double mTrackTemp;
    Vec3 mWind;
    double mMinPathWetness;
    double mMaxPathWetness;
    unsigned char mGameMode;
    bool mIsPasswordProtected;
    unsigned short mServerPort;
    unsigned long mServerPublicIP;
    long mMaxPlayers;
    char mServerName[32];
    float mStartET;
    double mAvgPathWetness;
    unsigned char mExpansion[200];
    unsigned char pointer2[8];
};

struct VehicleScoring
{
    long mID;
    char mDriverName[32];
    char mVehicleName[64];
    short mTotalLaps;
    signed char mSector;
    signed char mFinishStatus;
    double mLapDist;
    double mPathLateral;
    double mTrackEdge;
    double mBestSector1;
    double mBestSector2;
    double mBestLapTime;
    double mLastSector1;
    double mLastSector2;
    double mLastLapTime;
    double mCurSector1;
    double mCurSector2;
    short mNumPitstops;
    short mNumPenalties;
    bool mIsPlayer;
    signed char mControl;
    bool mInPits;
    unsigned char mPlace;
    char mVehicleClass[32];
    double mTimeBehindNext;
    long mLapsBehindNext;
    double mTimeBehindLeader;
    long mLapsBehindLeader;
    double mLapStartET;
    Vec3 mPos;
    Vec3 mLocalVel;
    Vec3 mLocalAccel;
    Vec3 mOri[3];
    Vec3 mLocalRot;
    Vec3 mLocalRotAccel;
    unsigned char mHeadlights;
    unsigned char mPitState;
    unsigned char mServerScored;
    unsigned char mIndividualPhase;
    long mQualification;
    double mTimeIntoLap;
    double mEstimatedLapTime;
    char mPitGroup[24];
    unsigned char mFlag;
    bool mUnderYellow;
    unsigned char mCountLapFlag;
    bool mInGarageStall;
    unsigned char mUpgradePack[16];
    float mPitLapDist;
    float mBestLapSector1;
    float mBestLapSector2;
    unsigned char mExpansion[48];
};

constexpr int kMaxMappedVehicles = 128;

struct ScoringBuffer
{
    unsigned long mVersionUpdateBegin; // incrementato prima della scrittura
    unsigned long mVersionUpdateEnd;   // incrementato a scrittura finita
    int mBytesUpdatedHint;
    ScoringInfo mScoringInfo;
    VehicleScoring mVehicles[kMaxMappedVehicles];
};

constexpr const char *kScoringMappingName = "$rFactor2SMMP_Scoring$";

// Buffer "$rFactor2SMMP_Graphics$" (rF2GraphicsInfoV02): posizione e tipo della telecamera.
// Il plugin lo pubblica solo se nel suo UnsubscribedBuffersMask il bit 32 e' spento.
struct GraphicsInfo
{
    Vec3 mCamPos;
    Vec3 mCamOri[3];
    unsigned char mHWND[8];
    double mAmbientRed;
    double mAmbientGreen;
    double mAmbientBlue;
    long mID;         // vettura inquadrata
    long mCameraType; // 0 = abitacolo TV, 1 = abitacolo, 2 = muso, 3 = esterna, 4 = bordo pista, 5+ = onboard
    unsigned char mExpansion[128];
};

struct GraphicsBuffer
{
    unsigned long mVersionUpdateBegin;
    unsigned long mVersionUpdateEnd;
    GraphicsInfo mGraphicsInfo;
};

constexpr const char *kGraphicsMappingName = "$rFactor2SMMP_Graphics$";
}

#pragma pack(pop)
