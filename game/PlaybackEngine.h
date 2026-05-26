#pragma once

#include "level/LevelData.h"
#include "Planet.h"
#include "audio/HitsoundManager.h"
#include <memory>
#include <vector>

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
    void syncToAudio(float audioPosSec, float offsetSec);  // drive from audio clock

    Planet* redPlanet()  { return m_redPlanet.get(); }
    Planet* bluePlanet() { return m_bluePlanet.get(); }

    bool isPlaying() const { return m_isPlaying; }
    float elapsedTimeMs() const { return (float)m_elapsedTime; }
    float timeInLevel() const;
    int currentTileIndex() const { return m_currentTileIndex; }

    const std::vector<double>& tileStartTimes() const { return m_tileStartTimes; }
    const std::vector<float>& tileBPMPerTile() const { return m_tileBPM; }
    float totalDuration() const;
    std::vector<double> getHitsoundTimestamps() const;
    std::vector<struct HitsoundTimestampGroup> getHitsoundTimestampGroups() const;
    void computePlanetTrails() const;
    void computePositionsAtTime(float t, glm::vec2& redOut, glm::vec2& blueOut) const;

private:
    const LevelData* m_level = nullptr;

    std::unique_ptr<Planet> m_redPlanet;
    std::unique_ptr<Planet> m_bluePlanet;
    bool m_showTrail = true;

    // Precalculated timing arrays (size = n tiles including extra)
    std::vector<double> m_tileStartTimes;  // double: prevents quantization at extreme BPM
    std::vector<float> m_tileDurations;
    std::vector<float> m_tileTotalAngles;
    std::vector<float> m_tileStartAngles;
    std::vector<float> m_tileBPM;
    std::vector<bool>  m_tileIsCW;
    std::vector<float> m_tileStartDist;
    std::vector<float> m_tileEndDist;

    bool   m_isPlaying = false;
    double m_elapsedTime = 0.0;   // double: sub-ns precision at 1000s total time
    double m_startWallClock = 0.0;  // wall-clock time when playback started
    int    m_currentTileIndex = 0;
    bool   m_reportedEnd = false;   // track if end-of-level log was emitted

    void precalculateTiming();
    int  findTileIndex(float timeInLevel) const;
    void updatePlanetPositions();
};
