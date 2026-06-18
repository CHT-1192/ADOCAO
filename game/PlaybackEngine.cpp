#include "PlaybackEngine.h"
#include "util/Logger.h"
#include <GLFW/glfw3.h>
#include <cmath>
#include <algorithm>
#include <future>
#include <thread>

void PlaybackEngine::init(const LevelData& level, bool showTrail) {
    m_level = &level;
    m_showTrail = showTrail;
    m_isPlaying = false;
    m_elapsedTime = 0.0;
    m_currentTileIndex = 0;

    m_redPlanet.reset();
    m_bluePlanet.reset();

    precalculateTiming();

    m_redPlanet = std::make_unique<Planet>(glm::vec3(1.0f, 0.0f, 0.0f), showTrail);
    m_bluePlanet = std::make_unique<Planet>(glm::vec3(0.0f, 0.0f, 1.0f), showTrail);
}

// Phase 2 worker: compute per-tile geometry for tiles [start, end).
// Each tile is independent — reads pre-computed state arrays and const level data.
static void precalcTileRange(
    const LevelData& level,
    const std::vector<uint8_t>& preIsCW,
    const std::vector<float>&   preBPM,
    const std::vector<float>&   preAngleDir,
    const std::vector<float>&   preExtraRot,
    std::vector<float>& outStartAngles,
    std::vector<float>& outTotalAngles,
    std::vector<float>& outDurations,
    std::vector<float>& outStartDist,
    std::vector<float>& outEndDist,
    int start, int end, int n)
{
    const auto& tiles = level.tiles;
    const auto& angleData = level.angleData;

    for (int i = start; i < end; i++) {
        bool isCW = (preIsCW[i] != 0);
        float currentBPM = preBPM[i];

        // ---- Start angle from tile positions ----
        float startAngle;
        if (i == 0) {
            startAngle = (level.settings.rotation + 180.0f) * 3.14159265f / 180.0f;
        } else {
            const auto& pivotPos = tiles[i].position;
            const auto& prevPos  = tiles[i - 1].position;
            startAngle = std::atan2(prevPos[1] - pivotPos[1], prevPos[0] - pivotPos[0]);
        }
        outStartAngles[i] = startAngle;

        // ---- Relative angle from absolute direction (ADOFAI-JS _parseAngle) ----
        double rawAngleData = (i < (int)angleData.size()) ? angleData[i] : 180.0;
        double relAngle;
        if (rawAngleData == 999.0) {
            relAngle = 0.0;
        } else {
            double delta = std::fmod((double)preAngleDir[i] - rawAngleData, 360.0);
            if (delta < 0) delta += 360.0;
            if (!isCW) {
                relAngle = 360.0 - delta;
                if (relAngle >= 360.0) relAngle -= 360.0;
            } else {
                relAngle = delta;
            }
            if (delta < 0.0001) relAngle = 360.0;
        }
        float totalAngle = (float)relAngle * 3.14159265f / 180.0f;
        if (isCW) totalAngle = -totalAngle;

        // Pause extra rotation
        if (isCW) totalAngle -= preExtraRot[i] * 2.0f * 3.14159265f;
        else      totalAngle += preExtraRot[i] * 2.0f * 3.14159265f;

        outTotalAngles[i] = totalAngle;

        // Duration: rotationAmount * 2 * secPerBeat
        float rotationAmount = std::abs(totalAngle) / (2.0f * 3.14159265f);
        float duration = rotationAmount * 2.0f * (60.0f / currentBPM);
        outDurations[i] = duration;

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
        outStartDist[i] = distToPrev;
        outEndDist[i]   = distToNext;
    }
}

// Precalculate per-tile timing arrays from absolute angleData (ADOFAI-JS _parseAngle algorithm).
// Splits into 4 phases: (0) actions-by-floor index, (1) sequential state propagation,
// (2) parallel tile geometry, (3) sequential prefix sum, (4) last-tile handling.
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

    // ---- Phase 0: Pre-index actions by floor (O(m), unchanged) ----
    std::vector<std::vector<size_t>> actionsByFloor(n);
    if (!m_level->actions.is_null() && m_level->actions.is_array()) {
        for (size_t j = 0; j < m_level->actions.size(); j++) {
            auto& a = m_level->actions[j];
            if (!a.is_object() || !a.contains("floor") || !a.contains("eventType")) continue;
            int floor = a["floor"].get<int>();
            if (floor >= 0 && floor < n) actionsByFloor[floor].push_back(j);
        }
    }

    // ---- Phase 1 (sequential): Forward-propagate state arrays ----
    // Compute isCW, BPM, angleDir, and Pause extra rotation for every tile.
    // These depend on all previous tiles' events, so must be serial.
    std::vector<uint8_t> preIsCW(n);
    std::vector<float>   preBPM(n);
    std::vector<float>   preAngleDir(n - 1);
    std::vector<float>   preExtraRot(n - 1, 0.0f);

    bool isCW = true;
    float currentBPM = m_level->settings.bpm;
    float angleDir = 180.0f;

    for (int i = 0; i < n - 1; i++) {
        preAngleDir[i] = angleDir;
        float extraRotation = 0.0f;

        for (size_t j : actionsByFloor[i]) {
            auto& a = m_level->actions[j];
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

        preIsCW[i] = isCW ? 1 : 0;
        preBPM[i] = currentBPM;
        preExtraRot[i] = extraRotation;

        // Propagate angleDir to next tile
        double rawAngleData = (i < (int)angleData.size()) ? angleData[i] : 180.0;
        if (rawAngleData == 999.0) {
            double prevAbs = (i > 0) ? angleData[i - 1] : 0.0;
            angleDir = (float)std::fmod(prevAbs, 360.0);
            if (angleDir < 0) angleDir += 360.0f;
        } else {
            angleDir = (float)std::fmod(rawAngleData + 180.0, 360.0);
            if (angleDir < 0) angleDir += 360.0f;
        }
    }
    // Save final state for the last (extra) tile
    preIsCW[n - 1] = isCW ? 1 : 0;
    preBPM[n - 1] = currentBPM;

    // ---- Phase 2 (parallel): Per-tile geometry ----
    // Each tile's computation is independent once Phase 1 state arrays are known.
    constexpr int PARALLEL_THRESHOLD = 256;
    int workItems = n - 1;  // tiles 0 through n-2

    if (workItems >= PARALLEL_THRESHOLD) {
        unsigned int hw = std::thread::hardware_concurrency();
        if (hw == 0) hw = 2;
        unsigned int numThreads = std::min(hw, (unsigned)(workItems / 64));
        if (numThreads < 2) numThreads = 2;
        size_t chunk = ((size_t)workItems + numThreads - 1) / numThreads;

        std::vector<std::future<void>> futures;
        for (unsigned int t = 0; t < numThreads; t++) {
            size_t s = t * chunk;
            size_t e = std::min(s + chunk, (size_t)workItems);
            if (s >= e) break;
            futures.push_back(std::async(std::launch::async,
                precalcTileRange,
                std::cref(*m_level),
                std::cref(preIsCW), std::cref(preBPM),
                std::cref(preAngleDir), std::cref(preExtraRot),
                std::ref(m_tileStartAngles), std::ref(m_tileTotalAngles),
                std::ref(m_tileDurations), std::ref(m_tileStartDist), std::ref(m_tileEndDist),
                (int)s, (int)e, n));
        }
        for (auto& f : futures) f.wait();
    } else {
        precalcTileRange(*m_level, preIsCW, preBPM, preAngleDir, preExtraRot,
                         m_tileStartAngles, m_tileTotalAngles,
                         m_tileDurations, m_tileStartDist, m_tileEndDist,
                         0, workItems, n);
    }

    // Copy state arrays to member vectors (sequential write, safe)
    for (int i = 0; i < n; i++) {
        m_tileIsCW[i] = (preIsCW[i] != 0);
        m_tileBPM[i]  = preBPM[i];
    }

    // ---- Phase 3 (sequential): Prefix sum of durations → tileStartTimes ----
    double totalTime = 0.0;
    for (int i = 0; i < n - 1; i++) {
        m_tileStartTimes[i] = totalTime;
        totalTime += m_tileDurations[i];
    }
    if (n > 0) m_tileStartTimes[n - 1] = totalTime;

    double preShiftTotal = totalTime;  // capture for log before shift

    // Shift so tileStartTimes[1] = 0
    if (n > 1) {
        double shift = m_tileStartTimes[1];
        for (int i = 0; i < n; i++) {
            m_tileStartTimes[i] -= shift;
        }
    }

    // ---- Phase 4 (sequential): Last tile (n-1) events + distances ----
    int lastIdx = n - 1;
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
          n - 1, preShiftTotal, *std::min_element(m_tileBPM.begin(), m_tileBPM.end()),
          *std::max_element(m_tileBPM.begin(), m_tileBPM.end()));
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
        m_redPlanet->position = glm::vec3(tiles[0].position[0], tiles[0].position[1], 9.5f);
        m_redPlanet->clearTrail();
    }
    if (m_bluePlanet && tiles.size() > 1) {
        m_bluePlanet->position = glm::vec3(tiles[1].position[0], tiles[1].position[1], 9.5f);
        m_bluePlanet->clearTrail();
    }

    updatePlanetPositions();
    LOG_I("Playback started");
}

void PlaybackEngine::startAt(double wallClockSec, float audioPosSec, float offsetSec) {
    if (m_isPlaying) return;
    m_isPlaying = true;
    m_elapsedTime = ((double)audioPosSec - (double)offsetSec) * 1000.0;
    if (m_elapsedTime < 0.0) m_elapsedTime = 0.0;
    m_startWallClock = wallClockSec - (m_elapsedTime / 1000.0);
    m_currentTileIndex = findTileIndex(timeInLevel());
    m_reportedEnd = false;

    // Place planets at correct mid-playback positions
    glm::vec2 r(0), b(0);
    computePositionsAtTime(timeInLevel(), r, b);
    if (m_redPlanet)  { m_redPlanet->position  = glm::vec3(r.x, r.y, 9.5f); m_redPlanet->clearTrail(); }
    if (m_bluePlanet) { m_bluePlanet->position = glm::vec3(b.x, b.y, 9.5f); m_bluePlanet->clearTrail(); }

    LOG_I("Playback started mid-level at tile %d, time=%.3fs", m_currentTileIndex, timeInLevel());
}

void PlaybackEngine::stop() {
    m_isPlaying = false;
    m_elapsedTime = 0.0;
    LOG_I("Playback stopped");
}

float PlaybackEngine::timeInLevel() const {
    float secPerBeat = 60.0f / m_level->settings.bpm;  // always use initial BPM
    float countdown = m_level->settings.countdownTicks * secPerBeat;
    return (float)(m_elapsedTime / 1000.0 - (double)countdown);
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

    float countdown = (float)m_level->settings.countdownTicks * (60.0f / m_level->settings.bpm);

    for (int i = 1; i < n; i++) {
        timestamps.push_back(m_tileStartTimes[i] + countdown);
    }
    return timestamps;
}

std::vector<HitsoundTimestampGroup> PlaybackEngine::getHitsoundTimestampGroups() const {
    std::vector<HitsoundTimestampGroup> groups;
    int n = (int)m_tileStartTimes.size();
    if (n < 2) return groups;

    float countdown = (float)m_level->settings.countdownTicks * (60.0f / m_level->settings.bpm);
    float offsetSec = m_level->settings.offset / 1000.0f;
    float defaultVol = m_level->settings.hitsoundVolume;
    const std::string& defaultType = m_level->settings.hitsound;

    // Map from (type, volume) key to group index
    std::map<std::pair<std::string, float>, size_t> groupMap;

    for (int i = 1; i < n; i++) {
        std::string type = (i < (int)m_level->tileHitsounds.size() && !m_level->tileHitsounds[i].empty())
            ? m_level->tileHitsounds[i] : defaultType;
        if (m_forceHitsounds && type == "None")
            type = "Kick";
        float vol = defaultVol;

        auto key = std::make_pair(type, vol);
        auto it = groupMap.find(key);
        if (it == groupMap.end()) {
            groupMap[key] = groups.size();
            groups.push_back({type, vol, {}});
        }
        groups[groupMap[key]].timestamps.push_back(m_tileStartTimes[i] + countdown + offsetSec);
    }
    return groups;
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
            pivotPlanet->position = glm::vec3(pivotPos[0], pivotPos[1], 9.5f);
        if (movingPlanet) {
            movingPlanet->position = glm::vec3(
                pivotPos[0] + std::cos(currentAngle) * dist,
                pivotPos[1] + std::sin(currentAngle) * dist,
                9.5f);
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
        pivotPlanet->position = glm::vec3(pivotPos[0], pivotPos[1], 9.5f);

    if (movingPlanet) {
        movingPlanet->position = glm::vec3(
            pivotPos[0] + std::cos(currentAngle) * currentDist,
            pivotPos[1] + std::sin(currentAngle) * currentDist,
            9.5f);
    }
}

void PlaybackEngine::computePositionsAtTime(float t, glm::vec2& redOut, glm::vec2& blueOut) const {
    const auto& tiles = m_level->tiles;
    int n = (int)tiles.size();
    if (n < 2) return;

    int tileIdx = findTileIndex(t);

    if (tileIdx >= n - 1) {
        int lastIdx = n - 1;
        const auto& pivotPos = tiles[lastIdx].position;
        float startAngle = 0.0f;
        if (lastIdx > 0) {
            const auto& prevPos = tiles[lastIdx - 1].position;
            startAngle = std::atan2(prevPos[1] - pivotPos[1], prevPos[0] - pivotPos[0]);
        }
        float extraTime = t - m_tileStartTimes[lastIdx];
        float rps = (m_tileBPM[lastIdx] / 60.0f) * 3.14159265f;
        float currentAngle = m_tileIsCW[lastIdx] ? (startAngle - extraTime * rps) : (startAngle + extraTime * rps);
        glm::vec2 mv(pivotPos[0] + std::cos(currentAngle), pivotPos[1] + std::sin(currentAngle));
        if (lastIdx % 2 == 0) { redOut = glm::vec2(pivotPos[0], pivotPos[1]); blueOut = mv; }
        else                  { blueOut = glm::vec2(pivotPos[0], pivotPos[1]); redOut = mv; }
        return;
    }

    bool isRed = (tileIdx % 2 == 0);
    const auto& pivotPos = tiles[tileIdx].position;
    float startTime = m_tileStartTimes[tileIdx];
    float duration = m_tileDurations[tileIdx];
    float progress = (duration > 0.0001f) ? (t - startTime) / duration : 1.0f;
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    float angle = m_tileStartAngles[tileIdx] + m_tileTotalAngles[tileIdx] * progress;
    float dist = m_tileStartDist[tileIdx] + (m_tileEndDist[tileIdx] - m_tileStartDist[tileIdx]) * progress;
    glm::vec2 pv(pivotPos[0], pivotPos[1]);
    glm::vec2 mv(pivotPos[0] + std::cos(angle) * dist, pivotPos[1] + std::sin(angle) * dist);
    if (isRed) { redOut = pv; blueOut = mv; }
    else       { blueOut = pv; redOut = mv; }
}

void PlaybackEngine::computePlanetTrails() const {
    if (!m_redPlanet || !m_bluePlanet) return;
    if (!m_redPlanet->trail || !m_bluePlanet->trail) return;

    float t = timeInLevel();
    const int samples = 80;
    std::vector<float> redXY(samples * 2), blueXY(samples * 2);

    for (int i = 0; i < samples; i++) {
        float tt = t - 0.4f + (0.4f * (float)i / (float)(samples - 1));
        glm::vec2 r(0), b(0);
        computePositionsAtTime(tt, r, b);
        redXY[i * 2] = r.x; redXY[i * 2 + 1] = r.y;
        blueXY[i * 2] = b.x; blueXY[i * 2 + 1] = b.y;
    }
    static int callCount = 0;
    if (callCount < 3) {
        LOG_I("Trail: t=%.3f red[0]=(%.1f,%.1f) red[79]=(%.1f,%.1f) blue[0]=(%.1f,%.1f) blue[79]=(%.1f,%.1f)",
              t, redXY[0], redXY[1], redXY[158], redXY[159],
              blueXY[0], blueXY[1], blueXY[158], blueXY[159]);
        callCount++;
    }
    m_redPlanet->setTrailPoints(redXY.data(), samples);
    m_bluePlanet->setTrailPoints(blueXY.data(), samples);
}

void PlaybackEngine::syncToAudio(float audioPosSec, float offsetSec) {
    if (!m_isPlaying) return;
    m_elapsedTime = ((double)audioPosSec - (double)offsetSec) * 1000.0;
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
