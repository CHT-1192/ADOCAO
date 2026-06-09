#include "PlaybackEngine.h"
#include "util/Logger.h"
#include <GLFW/glfw3.h>
#include <cmath>
#include <algorithm>

void PlaybackEngine::init(const LevelData& level, bool showTrail) {
    m_level = &level;
    m_showTrail = showTrail;
    m_isPlaying = false;
    m_elapsedTime = 0.0;
    m_currentTileIndex = 0;

    m_redPlanet.reset();
    m_bluePlanet.reset();

    precalculateTiming();

    // Cache invariant timeInLevel constants
    m_secPerBeat  = 60.0f / m_level->settings.bpm;
    m_countdownSec = m_level->settings.countdownTicks * m_secPerBeat;
    m_offsetSec   = m_level->settings.offset / 1000.0f;

    m_redPlanet = std::make_unique<Planet>(glm::vec3(1.0f, 0.0f, 0.0f), showTrail);
    m_bluePlanet = std::make_unique<Planet>(glm::vec3(0.0f, 0.0f, 1.0f), showTrail);
}

// Precalculate per-tile timing arrays from absolute angleData (ADOFAI-JS _parseAngle algorithm).
void PlaybackEngine::precalculateTiming() {
    const auto& tiles = m_level->tiles;
    const auto& angleData = m_level->angleData;
    int n = (int)tiles.size();
    if (n < 2) return;

    m_tileStartTimes.resize(n);
    m_tileTiming.resize(n);

    bool isCW = true;
    float currentBPM = m_level->settings.bpm;
    double totalTime = 0.0;
    float angleDir = 180.0f;
    constexpr float kPi = 3.14159265358979f;

    // Pre-index actions by floor — only allocate when actions exist
    std::vector<std::vector<size_t>> actionsByFloor;
    if (!m_level->actions.is_null() && m_level->actions.is_array() && m_level->actions.size() > 0) {
        actionsByFloor.resize(n);
        for (size_t j = 0; j < m_level->actions.size(); j++) {
            auto& a = m_level->actions[j];
            if (!a.is_object() || !a.contains("floor") || !a.contains("eventType")) continue;
            int floor = a["floor"].get<int>();
            if (floor >= 0 && floor < n) actionsByFloor[floor].push_back(j);
        }
    }

    // Pre-compute distances once per edge (each edge distance used as distToPrev[i+1] and distToNext[i])
    std::vector<float> edgeDist(n);  // edgeDist[i] = distance from tile i-1 to i
    edgeDist[0] = 1.0f;
    for (int i = 1; i < n; i++) {
        const auto& p  = tiles[i].position;
        const auto& pp = tiles[i - 1].position;
        float dx = p[0] - pp[0], dy = p[1] - pp[1];
        float d = std::sqrt(dx * dx + dy * dy);
        edgeDist[i] = (d > 0.01f) ? d : 1.0f;
    }

    for (int i = 0; i < n - 1; i++) {
        float extraRotation = 0.0f;

        // Process events on this floor
        if (i < (int)actionsByFloor.size()) {
            for (size_t j : actionsByFloor[i]) {
                auto& a = m_level->actions[j];
                std::string etype = a["eventType"].get<std::string>();
                if (etype == "Twirl") {
                    isCW = !isCW;
                } else if (etype == "SetSpeed") {
                    std::string stype = a.value("speedType", std::string("Bpm"));
                    if (stype == "Multiplier")
                        currentBPM *= a.value("bpmMultiplier", 1.0f);
                    else
                        currentBPM = a.value("beatsPerMinute", currentBPM);
                } else if (etype == "Pause") {
                    extraRotation += a.value("duration", 0.0f) / 2.0f;
                }
            }
        }

        m_tileTiming[i].isCW = isCW;
        m_tileTiming[i].bpm  = currentBPM;

        // Start angle from tile positions
        float startAngle;
        if (i == 0) {
            startAngle = (m_level->settings.rotation + 180.0f) * kPi / 180.0f;
        } else {
            const auto& pivotPos = tiles[i].position;
            const auto& prevPos  = tiles[i - 1].position;
            startAngle = std::atan2(prevPos[1] - pivotPos[1], prevPos[0] - pivotPos[0]);
        }
        m_tileTiming[i].startAngle = startAngle;

        // Relative angle from absolute direction (ADOFAI-JS _parseAngle)
        double rawAngleData = (i < (int)angleData.size()) ? angleData[i] : 180.0;
        double relAngle;
        if (rawAngleData == 999.0) {
            relAngle = 0.0;
            double prevAbs = (i > 0) ? angleData[i - 1] : 0.0;
            angleDir = std::fmod(prevAbs, 360.0);
            if (angleDir < 0) angleDir += 360.0;
        } else {
            double delta = std::fmod(angleDir - rawAngleData, 360.0);
            if (delta < 0) delta += 360.0f;
            if (!isCW) {
                relAngle = 360.0 - delta;
                if (relAngle >= 360.0) relAngle -= 360.0;
            } else {
                relAngle = delta;
            }
            if (delta < 0.0001) relAngle = 360.0;
            angleDir = std::fmod(rawAngleData + 180.0, 360.0);
            if (angleDir < 0) angleDir += 360.0;
        }
        float totalAngle = (float)(relAngle * kPi / 180.0);
        if (isCW) totalAngle = -totalAngle;

        // Pause extra rotation
        float extra = extraRotation * 2.0f * kPi;
        totalAngle += isCW ? -extra : extra;

        m_tileTiming[i].totalAngle = totalAngle;

        // Duration
        float rotationAmount = std::abs(totalAngle) / (2.0f * kPi);
        float duration = rotationAmount * 2.0f * (60.0f / currentBPM);
        m_tileTiming[i].duration = duration;

        // Distances: use pre-computed edge distances
        m_tileTiming[i].startDist = edgeDist[i];
        m_tileTiming[i].endDist   = (i + 1 < n) ? edgeDist[i + 1] : 1.0f;

        // Cumulative timing (apply shift inline to avoid second O(n) pass)
        m_tileStartTimes[i] = totalTime;
        totalTime += duration;
    }

    // Extra tile start time
    if (n > 0) m_tileStartTimes[n - 1] = totalTime;

    // Shift so tileStartTimes[1] = 0 (apply inline)
    if (n > 1) {
        double shift = m_tileStartTimes[1];
        for (int i = 0; i < n; i++)
            m_tileStartTimes[i] -= shift;
    }

    // Process events on last tile using pre-indexed lookup
    int lastIdx = n - 1;
    m_tileTiming[lastIdx].isCW      = isCW;
    m_tileTiming[lastIdx].bpm       = currentBPM;
    m_tileTiming[lastIdx].duration  = 0.0f;
    m_tileTiming[lastIdx].startAngle = (lastIdx > 0) ? m_tileTiming[lastIdx - 1].startAngle : 0.0f;

    if (lastIdx < (int)actionsByFloor.size()) {
        for (size_t j : actionsByFloor[lastIdx]) {
            auto& a = m_level->actions[j];
            std::string etype = a["eventType"].get<std::string>();
            if (etype == "Twirl") {
                m_tileTiming[lastIdx].isCW = !m_tileTiming[lastIdx].isCW;
            } else if (etype == "SetSpeed") {
                std::string stype = a.value("speedType", std::string("Bpm"));
                if (stype == "Multiplier")
                    m_tileTiming[lastIdx].bpm *= a.value("bpmMultiplier", 1.0f);
                else
                    m_tileTiming[lastIdx].bpm = a.value("beatsPerMinute", m_tileTiming[lastIdx].bpm);
            }
        }
    }

    // Start/end distances for last tile (endDist = startDist since no next tile)
    m_tileTiming[lastIdx].startDist = edgeDist[lastIdx];
    m_tileTiming[lastIdx].endDist   = edgeDist[lastIdx];

    // Cache BPM view for public accessor
    m_tileBPMCache.reserve(n);
    for (int i = 0; i < n; i++) m_tileBPMCache.push_back(m_tileTiming[i].bpm);

    LOG_I("PlaybackEngine: %d tiles, total duration %.2fs",
          n - 1, totalTime);
}

void PlaybackEngine::start(double wallClockSec) {
    if (m_isPlaying) return;
    m_isPlaying = true;
    m_elapsedTime = 0.0;
    m_startWallClock = wallClockSec;
    m_currentTileIndex = 0;
    m_reportedEnd = false;

    LOG_I("Playback started at t=%.3fs, countdown=%.1f beats", wallClockSec,
          m_level->settings.countdownTicks * (60.0f / m_level->settings.bpm));

    const auto& tiles = m_level->tiles;
    if (m_redPlanet && !tiles.empty()) {
        m_redPlanet->position = glm::vec3(tiles[0].position[0], tiles[0].position[1], 3.0f);
        m_redPlanet->clearTrail();
    }
    if (m_bluePlanet && tiles.size() > 1) {
        m_bluePlanet->position = glm::vec3(tiles[1].position[0], tiles[1].position[1], 3.0f);
        m_bluePlanet->clearTrail();
    }

    // Compute correct initial positions (matching reference: calls updatePlanetPositions in start)
    updatePlanetPositions();

    LOG_I("Playback started");
}

void PlaybackEngine::stop() {
    m_isPlaying = false;
    m_elapsedTime = 0.0;
    LOG_I("Playback stopped");
}

float PlaybackEngine::timeInLevel() const {
    return (float)(m_elapsedTime / 1000.0 - (double)m_countdownSec - (double)m_offsetSec);
}

float PlaybackEngine::totalDuration() const {
    int n = (int)m_tileStartTimes.size();
    if (n < 2) return 0.0f;
    return m_tileStartTimes[n - 1] + 10.0f;  // last tile time + buffer
}

std::vector<double> PlaybackEngine::getHitsoundTimestamps() const {
    std::vector<double> timestamps;
    int n = (int)m_tileStartTimes.size();
    if (n < 2) return timestamps;

    double offset = (double)m_countdownSec + (double)m_offsetSec;
    timestamps.reserve(n - 1);
    for (int i = 1; i < n; i++) {
        timestamps.push_back(m_tileStartTimes[i] + offset);
    }
    return timestamps;
}

std::vector<HitsoundTimestampGroup> PlaybackEngine::getHitsoundTimestampGroups() const {
    std::vector<HitsoundTimestampGroup> groups;
    int n = (int)m_tileStartTimes.size();
    if (n < 2) return groups;

    float defaultVol = m_level->settings.hitsoundVolume;
    const std::string& defaultType = m_level->settings.hitsound;

    // Small-group linear scan (beats map lookup for 1-5 distinct groups)
    struct Key { std::string type; float vol; };

    for (int i = 1; i < n; i++) {
        const std::string& type = (i < (int)m_level->tileHitsounds.size() && !m_level->tileHitsounds[i].empty())
            ? m_level->tileHitsounds[i] : defaultType;

        // Linear scan — groups vector is tiny (typically 1-5 entries)
        size_t gi = 0;
        for (; gi < groups.size(); gi++) {
            if (groups[gi].type == type && groups[gi].volume == defaultVol) break;
        }
        if (gi == groups.size())
            groups.push_back({type, defaultVol, {}});
        groups[gi].timestamps.push_back(m_tileStartTimes[i] + (double)m_countdownSec);
    }
    return groups;
}

int PlaybackEngine::findTileIndex(float t) const {
    int n = (int)m_tileStartTimes.size();
    if (n < 2) return 0;

    // Temporal hint: check if still in current or next tile (O(1) in steady playback)
    int hint = m_currentTileIndex;
    if (hint >= 0 && hint < n - 1) {
        if (t >= m_tileStartTimes[hint] && t < m_tileStartTimes[hint + 1])
            return hint;
        // Fast-forward linear probe (most common case: advancing 1 tile per frame)
        if (hint + 1 < n - 1 && t >= m_tileStartTimes[hint + 1]) {
            int probe = hint + 1;
            while (probe + 1 < n && t >= m_tileStartTimes[probe + 1]) probe++;
            return (probe < n) ? probe : (n - 1);
        }
    }
    // Fallback binary search for seek/rewind
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (m_tileStartTimes[mid] <= t) lo = mid + 1;
        else hi = mid - 1;
    }
    int idx = (hi < 0) ? 0 : ((hi >= n) ? n - 1 : hi);
    return idx;
}

void PlaybackEngine::updatePlanetPositions() {
    const auto& tiles = m_level->tiles;
    int n = (int)tiles.size();
    if (n < 2) return;

    float t = timeInLevel();
    int tileIdx = findTileIndex(t);
    m_currentTileIndex = tileIdx;

    // Past last tile: infinite rotation around the extra tile
    if (tileIdx >= n - 1) {
        if (!m_reportedEnd) {
            m_reportedEnd = true;
            double wallNow = glfwGetTime();
            LOG_I("Planet reached end: tileTime=%.3fs wallTime=%.3fs wallElapsed=%.3fs",
                  timeInLevel(), wallNow, wallNow - m_startWallClock);
        }
        int lastIdx = n - 1;
        const auto& pivotPos = tiles[lastIdx].position;
        const auto& tm = m_tileTiming[lastIdx];

        float startAngle = 0.0f;
        if (lastIdx > 0) {
            const auto& prevPos = tiles[lastIdx - 1].position;
            startAngle = std::atan2(prevPos[1] - pivotPos[1], prevPos[0] - pivotPos[0]);
        }

        float extraTime = t - (float)m_tileStartTimes[lastIdx];
        float radiansPerSec = (tm.bpm / 60.0f) * 3.14159265f;
        float extraAngle = extraTime * radiansPerSec;
        float currentAngle = tm.isCW ? (startAngle - extraAngle) : (startAngle + extraAngle);
        constexpr float kFixedDist = 1.0f;

        bool isRedPivot = (lastIdx % 2 == 0);
        Planet* pivotPlanet  = isRedPivot ? m_redPlanet.get() : m_bluePlanet.get();
        Planet* movingPlanet = isRedPivot ? m_bluePlanet.get() : m_redPlanet.get();

        if (pivotPlanet)
            pivotPlanet->position = glm::vec3(pivotPos[0], pivotPos[1], 3.0f);
        if (movingPlanet) {
            movingPlanet->position = glm::vec3(
                pivotPos[0] + std::cos(currentAngle) * kFixedDist,
                pivotPos[1] + std::sin(currentAngle) * kFixedDist, 3.0f);
        }
        return;
    }

    // Normal rotation — read all timing data from one cache line (AoS layout)
    const auto& pivotPos = tiles[tileIdx].position;
    const auto& tm = m_tileTiming[tileIdx];
    float startTime = (float)m_tileStartTimes[tileIdx];
    float progress = (tm.duration > 0.0001f) ? (t - startTime) / tm.duration : 1.0f;
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;

    float currentAngle = tm.startAngle + tm.totalAngle * progress;
    float currentDist  = tm.startDist + (tm.endDist - tm.startDist) * progress;

    bool isRedPivot = (tileIdx % 2 == 0);
    Planet* pivotPlanet  = isRedPivot ? m_redPlanet.get() : m_bluePlanet.get();
    Planet* movingPlanet = isRedPivot ? m_bluePlanet.get() : m_redPlanet.get();

    if (pivotPlanet)
        pivotPlanet->position = glm::vec3(pivotPos[0], pivotPos[1], 3.0f);

    if (movingPlanet) {
        movingPlanet->position = glm::vec3(
            pivotPos[0] + std::cos(currentAngle) * currentDist,
            pivotPos[1] + std::sin(currentAngle) * currentDist, 3.0f);
    }
}

void PlaybackEngine::syncToAudio(float audioPosSec) {
    if (!m_isPlaying) return;
    m_elapsedTime = (double)audioPosSec * 1000.0;
    if (m_elapsedTime < 0.0) m_elapsedTime = 0.0;

    updatePlanetPositions();

    float tSec = m_elapsedTime / 1000.0f;
    if (m_redPlanet)  m_redPlanet->update(tSec);
    if (m_bluePlanet) m_bluePlanet->update(tSec);
}

void PlaybackEngine::updateWallClock(double wallClockSec) {
    if (!m_isPlaying) return;
    m_elapsedTime = (wallClockSec - m_startWallClock) * 1000.0;
    if (m_elapsedTime < 0.0f) m_elapsedTime = 0.0;

    updatePlanetPositions();

    float tSec = m_elapsedTime / 1000.0f;
    if (m_redPlanet)  m_redPlanet->update(tSec);
    if (m_bluePlanet) m_bluePlanet->update(tSec);
}

void PlaybackEngine::update(float deltaMs) {
    if (!m_isPlaying) return;
    m_elapsedTime += deltaMs;

    updatePlanetPositions();

    float tSec = m_elapsedTime / 1000.0f;
    if (m_redPlanet)
        m_redPlanet->update(tSec);
    if (m_bluePlanet)
        m_bluePlanet->update(tSec);
}
