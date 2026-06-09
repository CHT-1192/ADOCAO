#pragma once

#include "level/LevelData.h"
#include "Planet.h"
#include "audio/HitsoundManager.h"
#include <memory>
#include <vector>

struct TileTiming {
    float duration;     // was m_tileDurations[i]
    float totalAngle;   // was m_tileTotalAngles[i]
    float startAngle;   // was m_tileStartAngles[i]
    float bpm;          // was m_tileBPM[i]
    float startDist;    // was m_tileStartDist[i]
    float endDist;      // was m_tileEndDist[i]
    bool  isCW;         // was m_tileIsCW[i]
};
// 6 floats + 1 bool + 3 padding = 28 bytes (fits one cache line with room to spare)

class PlaybackEngine {
public:
    PlaybackEngine() = default;
    ~PlaybackEngine() = default;

    PlaybackEngine(const PlaybackEngine&) = delete;
    PlaybackEngine& operator=(const PlaybackEngine&) = delete;

    void init(const LevelData& level, bool showTrail = true);

    void start(double wallClockSec);
    void stop();
    void update(float deltaMs);
    void updateWallClock(double wallClockSec);  // jump to absolute time (window drag/sleep)
    void syncToAudio(float audioPosSec);  // drive from audio clock (music starts at 0)

    Planet* redPlanet()  { return m_redPlanet.get(); }
    Planet* bluePlanet() { return m_bluePlanet.get(); }

    bool isPlaying() const { return m_isPlaying; }
    float elapsedTimeMs() const { return (float)m_elapsedTime; }
    float timeInLevel() const;
    int currentTileIndex() const { return m_currentTileIndex; }

    const std::vector<double>& tileStartTimes() const { return m_tileStartTimes; }
    const std::vector<float>& tileBPMPerTile() const { return m_tileBPMCache; }
    float totalDuration() const;
    double startWallClock() const { return m_startWallClock; }
    std::vector<double> getHitsoundTimestamps() const;
    std::vector<struct HitsoundTimestampGroup> getHitsoundTimestampGroups() const;

private:
    const LevelData* m_level = nullptr;

    std::unique_ptr<Planet> m_redPlanet;
    std::unique_ptr<Planet> m_bluePlanet;
    bool m_showTrail = true;

    // Precalculated timing arrays (size = n tiles including extra)
    std::vector<double> m_tileStartTimes;  // double: prevents quantization, used by binary search
    std::vector<TileTiming> m_tileTiming;    // AoS: cache-friendly per-tile data
    std::vector<float> m_tileBPMCache;       // cached BPM view (for public accessor)

    // Cached invariant constants (computed once in init)
    float m_secPerBeat = 1.0f;
    float m_countdownSec = 0.0f;
    float m_offsetSec = 0.0f;

    bool   m_isPlaying = false;
    double m_elapsedTime = 0.0;   // double: sub-ns precision at 1000s total time
    double m_startWallClock = 0.0;  // wall-clock time when playback started
    int    m_currentTileIndex = 0;
    bool   m_reportedEnd = false;   // track if end-of-level log was emitted

    void precalculateTiming();
    int  findTileIndex(float timeInLevel) const;
    void updatePlanetPositions();
};
