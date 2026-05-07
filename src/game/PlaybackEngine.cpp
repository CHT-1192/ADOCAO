#include "PlaybackEngine.h"
#include "../util/Logger.h"
#include <cmath>
#include <algorithm>

void PlaybackEngine::init(const LevelData& level, bool showTrail) {
    m_level = &level;
    m_showTrail = showTrail;
    m_isPlaying = false;
    m_elapsedTime = 0.0f;
    m_currentTileIndex = 0;

    m_redPlanet.reset();
    m_bluePlanet.reset();

    precalculateTiming();

    m_redPlanet = std::make_unique<Planet>(glm::vec3(1.0f, 0.0f, 0.0f), showTrail);
    m_bluePlanet = std::make_unique<Planet>(glm::vec3(0.0f, 0.0f, 1.0f), showTrail);
}

// Precalculate per-tile timing arrays from absolute angleData (ADOFAI-JS _parseAngle algorithm).
// Computes relative rotation, duration, start times, and processes events (Twirl/SetSpeed/Pause).
void PlaybackEngine::precalculateTiming() {
    const auto& tiles = m_level->tiles;
    const auto& angleData = m_level->angleData;
    int n = (int)tiles.size();
    if (n < 2) return;

    m_tileStartTimes.resize(n);
    m_tileDurations.resize(n);
    m_tileTotalAngles.resize(n);
    m_tileStartAngles.resize(n);
    m_tileBPM.resize(n);
    m_tileIsCW.resize(n);
    m_tileStartDist.resize(n);
    m_tileEndDist.resize(n);

    bool isCW = true;
    float currentBPM = m_level->settings.bpm;
    float totalTime = 0.0f;
    float angleDir = 180.0f;  // entry direction tracker (ADOFAI-JS _parseAngle)

    for (int i = 0; i < n - 1; i++) {
        float extraRotation = 0.0f;

        // Process events on this floor
        if (!m_level->actions.is_null() && m_level->actions.is_array()) {
            for (size_t j = 0; j < m_level->actions.size(); j++) {
                auto& a = m_level->actions[j];
                if (!a.is_object() || !a.contains("floor") || !a.contains("eventType")) continue;
                if (a["floor"].get<int>() != i) continue;

                std::string etype = a["eventType"].get<std::string>();
                if (etype == "Twirl") {
                    isCW = !isCW;
                } else if (etype == "SetSpeed") {
                    std::string stype = a.value("speedType", std::string("Bpm"));
                    if (stype == "Multiplier") {
                        currentBPM *= a.value("bpmMultiplier", 1.0f);
                    } else {
                        currentBPM = a.value("beatsPerMinute", currentBPM);
                    }
                } else if (etype == "Pause") {
                    float dur = a.value("duration", 0.0f);
                    extraRotation += dur / 2.0f;
                }
            }
        }

        m_tileIsCW[i] = isCW;
        m_tileBPM[i] = currentBPM;

        // --- Geometry: start angle from tile positions ---
        float startAngle;
        if (i == 0) {
            startAngle = (m_level->settings.rotation + 180.0f) * 3.14159265f / 180.0f;
        } else {
            const auto& pivotPos = tiles[i].position;
            const auto& prevPos  = tiles[i - 1].position;
            startAngle = std::atan2(prevPos[1] - pivotPos[1], prevPos[0] - pivotPos[0]);
        }
        m_tileStartAngles[i] = startAngle;

        // --- Relative angle: absolute direction → rotation amount (ADOFAI-JS _parseAngle) ---
        float rawAngleData = (i < (int)angleData.size()) ? angleData[i] : 180.0f;
        float relAngle;
        if (rawAngleData == 999.0f) {
            // Midspin: continue in same direction, 0 rotation (instant pass-through)
            relAngle = 0.0f;
            float prevAbs = (i > 0) ? angleData[i - 1] : 0.0f;
            angleDir = std::fmod(prevAbs, 360.0f);
            if (angleDir < 0) angleDir += 360.0f;
        } else {
            float delta = std::fmod(angleDir - rawAngleData, 360.0f);
            if (delta < 0) delta += 360.0f;
            // Twirl (isCW=false): planets go the long way around (360-delta)
            if (!isCW) {
                relAngle = 360.0f - delta;
                if (relAngle >= 360.0f) relAngle -= 360.0f;
            } else {
                relAngle = delta;
            }
            if (relAngle < 0.01f) relAngle = 360.0f;  // delta==0 → full rotation
            angleDir = std::fmod(rawAngleData + 180.0f, 360.0f);
            if (angleDir < 0) angleDir += 360.0f;
        }
        float totalAngle = relAngle * 3.14159265f / 180.0f;
        if (isCW) totalAngle = -totalAngle;

        // Pause extra rotation
        if (isCW) totalAngle -= extraRotation * 2.0f * 3.14159265f;
        else      totalAngle += extraRotation * 2.0f * 3.14159265f;

        m_tileTotalAngles[i] = totalAngle;

        // Duration: rotationAmount * 2 * secPerBeat
        float rotationAmount = std::abs(totalAngle) / (2.0f * 3.14159265f);
        float duration = rotationAmount * 2.0f * (60.0f / currentBPM);
        m_tileDurations[i] = duration;

        // Start/end distances
        float distToPrev = 1.0f, distToNext = 1.0f;
        const auto& p = tiles[i].position;
        if (i > 0) {
            const auto& pp = tiles[i - 1].position;
            float dx = p[0] - pp[0], dy = p[1] - pp[1];
            distToPrev = std::sqrt(dx * dx + dy * dy);
            if (distToPrev < 0.01f) distToPrev = 1.0f;
        }
        if (i + 1 < n) {
            const auto& np = tiles[i + 1].position;
            float dx = np[0] - p[0], dy = np[1] - p[1];
            distToNext = std::sqrt(dx * dx + dy * dy);
            if (distToNext < 0.01f) distToNext = 1.0f;
        }
        m_tileStartDist[i] = distToPrev;
        m_tileEndDist[i]   = distToNext;

        // Cumulative timing (reference: set tileStartTimes[i+1])
        m_tileStartTimes[i] = totalTime;
        totalTime += duration;
    }
    // Set the extra tile's start time to total duration (reference line 602)
    if (n > 0) m_tileStartTimes[n - 1] = totalTime;

    // Shift so tileStartTimes[1] = 0 (reference lines 605-611)
    if (n > 1) {
        float shift = m_tileStartTimes[1];
        for (int i = 0; i < n; i++) {
            m_tileStartTimes[i] -= shift;
        }
    }

    // Process events on last tile (for infinite rotation BPM)
    int lastIdx = n - 1;
    m_tileIsCW[lastIdx] = isCW;
    m_tileBPM[lastIdx] = currentBPM;
    m_tileDurations[lastIdx] = 0.0f;
    m_tileStartAngles[lastIdx] = (lastIdx > 0) ? m_tileStartAngles[lastIdx - 1] : 0.0f;

    if (!m_level->actions.is_null() && m_level->actions.is_array()) {
        for (size_t j = 0; j < m_level->actions.size(); j++) {
            auto& a = m_level->actions[j];
            if (!a.is_object() || !a.contains("floor") || !a.contains("eventType")) continue;
            if (a["floor"].get<int>() != lastIdx) continue;

            std::string etype = a["eventType"].get<std::string>();
            if (etype == "Twirl") m_tileIsCW[lastIdx] = !m_tileIsCW[lastIdx];
            else if (etype == "SetSpeed") {
                std::string stype = a.value("speedType", std::string("Bpm"));
                if (stype == "Multiplier") {
                    m_tileBPM[lastIdx] *= a.value("bpmMultiplier", 1.0f);
                } else {
                    m_tileBPM[lastIdx] = a.value("beatsPerMinute", m_tileBPM[lastIdx]);
                }
            }
        }
    }

    // Start/end distances for last tile
    {
        const auto& p = tiles[lastIdx].position;
        if (lastIdx > 0) {
            const auto& pp = tiles[lastIdx - 1].position;
            float dx = p[0] - pp[0], dy = p[1] - pp[1];
            float d = std::sqrt(dx * dx + dy * dy);
            m_tileStartDist[lastIdx] = d > 0.01f ? d : 1.0f;
        }
        m_tileEndDist[lastIdx] = m_tileStartDist[lastIdx];
    }

    LOG_I("PlaybackEngine: %d tiles, total duration %.2fs, bpm range %.0f-%.0f",
          n - 1, totalTime, *std::min_element(m_tileBPM.begin(), m_tileBPM.end()),
          *std::max_element(m_tileBPM.begin(), m_tileBPM.end()));
}

void PlaybackEngine::start() {
    if (m_isPlaying) return;
    m_isPlaying = true;
    m_elapsedTime = 0.0f;
    m_currentTileIndex = 0;

    const auto& tiles = m_level->tiles;
    if (m_redPlanet && !tiles.empty()) {
        m_redPlanet->position = glm::vec3(tiles[0].position[0], tiles[0].position[1], 1.0f);
        m_redPlanet->clearTrail();
    }
    if (m_bluePlanet && tiles.size() > 1) {
        m_bluePlanet->position = glm::vec3(tiles[1].position[0], tiles[1].position[1], 1.0f);
        m_bluePlanet->clearTrail();
    }

    // Compute correct initial positions (matching reference: calls updatePlanetPositions in start)
    updatePlanetPositions();

    LOG_I("Playback started");
}

void PlaybackEngine::stop() {
    m_isPlaying = false;
    m_elapsedTime = 0.0f;
    LOG_I("Playback stopped");
}

float PlaybackEngine::timeInLevel() const {
    float bpm = (m_tileBPM.size() > 0) ? m_tileBPM[0] : m_level->settings.bpm;
    float secPerBeat = 60.0f / bpm;
    float countdown = m_level->settings.countdownTicks * secPerBeat;
    return m_elapsedTime / 1000.0f - countdown;
}

float PlaybackEngine::totalDuration() const {
    int n = (int)m_tileStartTimes.size();
    if (n < 2) return 0.0f;
    return m_tileStartTimes[n - 1] + 10.0f;  // last tile time + buffer
}

std::vector<float> PlaybackEngine::getHitsoundTimestamps() const {
    std::vector<float> timestamps;
    int n = (int)m_tileStartTimes.size();
    if (n < 2) return timestamps;

    // tileStartTimes are relative to tile 1 (tileStartTimes[1]=0 after shift).
    // Add countdown duration so timestamps are relative to space-press time.
    float bpm = (m_tileBPM.size() > 0) ? m_tileBPM[0] : m_level->settings.bpm;
    float countdown = (float)m_level->settings.countdownTicks * (60.0f / bpm);

    for (int i = 1; i < n; i++) {
        timestamps.push_back(m_tileStartTimes[i] + countdown);
    }
    return timestamps;
}

int PlaybackEngine::findTileIndex(float t) const {
    int n = (int)m_tileStartTimes.size();
    if (n < 2) return 0;

    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (m_tileStartTimes[mid] <= t) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    int idx = hi;
    if (idx < 0) idx = 0;
    if (idx >= n) idx = n - 1;
    return idx;
}

void PlaybackEngine::updatePlanetPositions() {
    const auto& tiles = m_level->tiles;
    int n = (int)tiles.size();
    if (n < 2) return;

    float t = timeInLevel();

    int tileIdx = findTileIndex(t);
    m_currentTileIndex = tileIdx;

    // Past last tile: infinite rotation around the extra tile (reference line 2417)
    if (tileIdx >= n - 1) {
        int lastIdx = n - 1;
        const auto& pivotPos = tiles[lastIdx].position;
        float bpm = m_tileBPM[lastIdx];
        bool cw = m_tileIsCW[lastIdx];

        // startAngle = angle from extra tile back to previous tile (reference lines 2431-2437)
        float startAngle = 0.0f;
        if (lastIdx > 0) {
            const auto& prevPos = tiles[lastIdx - 1].position;
            startAngle = std::atan2(prevPos[1] - pivotPos[1], prevPos[0] - pivotPos[0]);
        }

        float extraTime = t - m_tileStartTimes[lastIdx];
        float radiansPerSec = (bpm / 60.0f) * 3.14159265f;
        float extraAngle = extraTime * radiansPerSec;
        float currentAngle = cw ? (startAngle - extraAngle) : (startAngle + extraAngle);
        float dist = 1.0f; // fixed distance for infinite rotation (reference line 2448)

        bool isRedPivot = (lastIdx % 2 == 0);
        Planet* pivotPlanet  = isRedPivot ? m_redPlanet.get() : m_bluePlanet.get();
        Planet* movingPlanet = isRedPivot ? m_bluePlanet.get() : m_redPlanet.get();

        if (pivotPlanet)
            pivotPlanet->position = glm::vec3(pivotPos[0], pivotPos[1], 1.0f);
        if (movingPlanet) {
            movingPlanet->position = glm::vec3(
                pivotPos[0] + std::cos(currentAngle) * dist,
                pivotPos[1] + std::sin(currentAngle) * dist,
                1.0f);
        }
        return;
    }

    // Normal rotation
    bool isRedPivot = (tileIdx % 2 == 0);
    Planet* pivotPlanet  = isRedPivot ? m_redPlanet.get() : m_bluePlanet.get();
    Planet* movingPlanet = isRedPivot ? m_bluePlanet.get() : m_redPlanet.get();

    const auto& pivotPos = tiles[tileIdx].position;
    float startTime = m_tileStartTimes[tileIdx];
    float duration = m_tileDurations[tileIdx];
    float progress = (duration > 0.0001f) ? (t - startTime) / duration : 1.0f;
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;

    float startAngle = m_tileStartAngles[tileIdx];
    float totalAngle = m_tileTotalAngles[tileIdx];
    float currentAngle = startAngle + totalAngle * progress;

    float startDist = m_tileStartDist[tileIdx];
    float endDist   = m_tileEndDist[tileIdx];
    float currentDist = startDist + (endDist - startDist) * progress;

    if (pivotPlanet)
        pivotPlanet->position = glm::vec3(pivotPos[0], pivotPos[1], 1.0f);

    if (movingPlanet) {
        movingPlanet->position = glm::vec3(
            pivotPos[0] + std::cos(currentAngle) * currentDist,
            pivotPos[1] + std::sin(currentAngle) * currentDist,
            1.0f);
    }
}

void PlaybackEngine::update(float deltaMs) {
    if (!m_isPlaying) return;
    m_elapsedTime += deltaMs;

    updatePlanetPositions();

    // Trail time: elapsed seconds (levelTime + countdown = elapsed/1000)
    float tSec = m_elapsedTime / 1000.0f;
    if (m_redPlanet)
        m_redPlanet->update(tSec);
    if (m_bluePlanet)
        m_bluePlanet->update(tSec);
}
