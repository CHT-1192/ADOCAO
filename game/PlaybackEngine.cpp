#include "PlaybackEngine.hpp"
#include "util/Logger.hpp"
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

    // Pre-roll: total time before tile 1 (≥360° / 2 beats on tile 0)
    {
        float secPerBeat = 60.0f / level.settings.bpm;
        float offsetSec = level.settings.offset / 1000.0f;
        float min2Beats = 2.0f * secPerBeat;  // 360° at initial BPM
        m_preRoll = std::max(offsetSec, min2Beats);
        m_audioStartOffset = m_preRoll - offsetSec;
    }

    m_redPlanet = std::make_unique<Planet>(glm::vec3(1.0f, 0.0f, 0.0f), showTrail);
    m_bluePlanet = std::make_unique<Planet>(glm::vec3(0.0f, 0.0f, 1.0f), showTrail);
}

// Phase 2 worker: compute per-tile geometry for tiles [start, end).
// Each tile is independent — reads pre-computed state arrays and const level data.
static void precalcTileRange(
    const LevelData& level,
    const std::vector<bool>&  tileIsCW,
    const std::vector<float>& tileBPM,
    const std::vector<float>& preAngleDir,
    const std::vector<float>& preExtraRot,
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
        bool isCW = tileIsCW[i];
        float currentBPM = tileBPM[i];

        float startAngle;
        if (i == 0) {
            startAngle = (level.settings.rotation + 180.0f) * 3.14159265f / 180.0f;
        } else {
            const auto& pivotPos = tiles[i].position;
            const auto& prevPos  = tiles[i - 1].position;
            startAngle = std::atan2(prevPos[1] - pivotPos[1], prevPos[0] - pivotPos[0]);
        }
        outStartAngles[i] = startAngle;

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

        if (isCW) totalAngle -= preExtraRot[i] * 2.0f * 3.14159265f;
        else      totalAngle += preExtraRot[i] * 2.0f * 3.14159265f;

        outTotalAngles[i] = totalAngle;

        float rotationAmount = std::abs(totalAngle) / (2.0f * 3.14159265f);
        float duration = rotationAmount * 2.0f * (60.0f / currentBPM);
        outDurations[i] = duration;

        float distToPrev = 1.0f, distToNext = 1.0f;
        const auto& p = tiles[i].position;
        if (i > 0) {
            const auto& pp = tiles[i - 1].position;
            float dx = (float)(p[0] - pp[0]), dy = (float)(p[1] - pp[1]);
            distToPrev = std::sqrt(dx * dx + dy * dy);
            if (distToPrev < 0.01f) distToPrev = 1.0f;
        }
        if (i + 1 < n) {
            const auto& np = tiles[i + 1].position;
            float dx = (float)(np[0] - p[0]), dy = (float)(np[1] - p[1]);
            distToNext = std::sqrt(dx * dx + dy * dy);
            if (distToNext < 0.01f) distToNext = 1.0f;
        }
        outStartDist[i] = distToPrev;
        outEndDist[i]   = distToNext;
    }
}

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

    // Phase 0: Pre-index actions by floor
    std::vector<std::vector<size_t>> actionsByFloor(n);
    if (!m_level->actions.is_null() && m_level->actions.is_array()) {
        for (size_t j = 0; j < m_level->actions.size(); j++) {
            auto& a = m_level->actions[j];
            if (!a.is_object() || !a.contains("floor") || !a.contains("eventType")) continue;
            int floor = a["floor"].get<int>();
            if (floor >= 0 && floor < n) actionsByFloor[floor].push_back(j);
        }
    }

    // Phase 1 (sequential): Write directly to m_tileIsCW/m_tileBPM (no temp vectors)
    std::vector<float> preAngleDir(n - 1);
    std::vector<float> preExtraRot(n - 1, 0.0f);

    bool isCW = true;
    float currentBPM = m_level->settings.bpm;
    float angleDir = 180.0f;

    for (int i = 0; i < n - 1; i++) {
        preAngleDir[i] = angleDir;
        float extraRotation = 0.0f;

        for (size_t j : actionsByFloor[i]) {
            auto& a = m_level->actions[j];
            const std::string& etype = a["eventType"].get_ref<const std::string&>();

            if (etype == "Twirl") {
                isCW = !isCW;
            } else if (etype == "SetSpeed") {
                if (a.contains("speedType")) {
                    const std::string& stype = a["speedType"].get_ref<const std::string&>();
                    if (stype == "Multiplier")
                        currentBPM *= a.value("bpmMultiplier", 1.0f);
                    else
                        currentBPM = a.value("beatsPerMinute", currentBPM);
                } else {
                    currentBPM = a.value("beatsPerMinute", currentBPM);
                }
            } else if (etype == "Pause") {
                float dur = a.value("duration", 0.0f);
                extraRotation += dur / 2.0f;
            }
        }

        m_tileIsCW[i] = isCW;
        m_tileBPM[i] = currentBPM;
        preExtraRot[i] = extraRotation;

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
    m_tileIsCW[n - 1] = isCW;
    m_tileBPM[n - 1] = currentBPM;

    // Phase 2 (parallel): Per-tile geometry
    constexpr int PARALLEL_THRESHOLD = 256;
    int workItems = n - 1;

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
                std::cref(m_tileIsCW), std::cref(m_tileBPM),
                std::cref(preAngleDir), std::cref(preExtraRot),
                std::ref(m_tileStartAngles), std::ref(m_tileTotalAngles),
                std::ref(m_tileDurations), std::ref(m_tileStartDist), std::ref(m_tileEndDist),
                (int)s, (int)e, n));
        }
        for (auto& f : futures) f.wait();
    } else {
        precalcTileRange(*m_level, m_tileIsCW, m_tileBPM, preAngleDir, preExtraRot,
                         m_tileStartAngles, m_tileTotalAngles,
                         m_tileDurations, m_tileStartDist, m_tileEndDist,
                         0, workItems, n);
    }

    // Phase 3: Prefix sum
    double totalTime = 0.0;
    for (int i = 0; i < n - 1; i++) {
        m_tileStartTimes[i] = totalTime;
        totalTime += m_tileDurations[i];
    }
    if (n > 0) m_tileStartTimes[n - 1] = totalTime;

    double preShiftTotal = totalTime;
    if (n > 1) {
        double shift = m_tileStartTimes[1];
        for (int i = 0; i < n; i++) m_tileStartTimes[i] -= shift;
    }

    // Phase 4: Last tile — use pre-built index instead of scanning all actions
    int lastIdx = n - 1;
    m_tileDurations[lastIdx] = 0.0f;
    m_tileStartAngles[lastIdx] = (lastIdx > 0) ? m_tileStartAngles[lastIdx - 1] : 0.0f;

    for (size_t j : actionsByFloor[lastIdx]) {
        auto& a = m_level->actions[j];
        const std::string& etype = a["eventType"].get_ref<const std::string&>();

        if (etype == "Twirl") {
            m_tileIsCW[lastIdx] = !m_tileIsCW[lastIdx];
        } else if (etype == "SetSpeed") {
            if (a.contains("speedType")) {
                const std::string& stype = a["speedType"].get_ref<const std::string&>();
                if (stype == "Multiplier")
                    m_tileBPM[lastIdx] *= a.value("bpmMultiplier", 1.0f);
                else
                    m_tileBPM[lastIdx] = a.value("beatsPerMinute", m_tileBPM[lastIdx]);
            } else {
                m_tileBPM[lastIdx] = a.value("beatsPerMinute", m_tileBPM[lastIdx]);
            }
        }
    }

    {
        const auto& p = tiles[lastIdx].position;
        if (lastIdx > 0) {
            const auto& pp = tiles[lastIdx - 1].position;
            float dx = (float)(p[0] - pp[0]), dy = (float)(p[1] - pp[1]);
            float d = std::sqrt(dx * dx + dy * dy);
            m_tileStartDist[lastIdx] = d > 0.01f ? d : 1.0f;
        }
        m_tileEndDist[lastIdx] = m_tileStartDist[lastIdx];
    }

    auto [minBPM, maxBPM] = std::minmax_element(m_tileBPM.begin(), m_tileBPM.end());
    LOG_D("PlaybackEngine: %d tiles, total duration %.2fs, bpm range %.0f-%.0f",
          n - 1, preShiftTotal, *minBPM, *maxBPM);
}

void PlaybackEngine::start(double wallClockSec) {
    if (m_isPlaying) return;
    m_isPlaying = true;
    m_elapsedTime = 0.0;
    m_startWallClock = wallClockSec;
    m_currentTileIndex = 0;
    m_reportedEnd = false;

    LOG_D("Playback started at t=%.3fs, countdown=%.1f beats", wallClockSec,
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
    LOG_D("Playback started");
}

void PlaybackEngine::startAt(double wallClockSec, float audioPosSec, float offsetSec) {
    if (m_isPlaying) return;
    m_isPlaying = true;
    m_elapsedTime = ((double)audioPosSec + (double)m_audioStartOffset) * 1000.0;
    if (m_elapsedTime < 0.0) m_elapsedTime = 0.0;
    m_startWallClock = wallClockSec - (m_elapsedTime / 1000.0);
    m_currentTileIndex = findTileIndex(timeInLevel());
    m_reportedEnd = false;

    glm::dvec2 r(0), b(0);
    computePositionsAtTime(timeInLevel(), r, b);
    if (m_redPlanet)  { m_redPlanet->position  = glm::vec3((float)r.x, (float)r.y, 9.5f); m_redPlanet->clearTrail(); }
    if (m_bluePlanet) { m_bluePlanet->position = glm::vec3((float)b.x, (float)b.y, 9.5f); m_bluePlanet->clearTrail(); }

    LOG_D("Playback started mid-level at tile %d, time=%.3fs", m_currentTileIndex, timeInLevel());
}

void PlaybackEngine::stop() {
    m_isPlaying = false;
    m_elapsedTime = 0.0;
    LOG_D("Playback stopped");
}

float PlaybackEngine::timeInLevel() const {
    return (float)(m_elapsedTime / 1000.0 - (double)m_preRoll);
}

float PlaybackEngine::totalDuration() const {
    int n = (int)m_tileStartTimes.size();
    if (n < 2) return 0.0f;
    return m_tileStartTimes[n - 1] + 10.0f;
}

std::vector<double> PlaybackEngine::getHitsoundTimestamps() const {
    std::vector<double> timestamps;
    int n = (int)m_tileStartTimes.size();
    if (n < 2) return timestamps;
    float offsetSec = m_level->settings.offset / 1000.0f;
    for (int i = 1; i < n; i++)
        timestamps.push_back(m_tileStartTimes[i] + offsetSec);
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

    std::map<std::pair<std::string, float>, size_t> groupMap;

    for (int i = 1; i < n; i++) {
        std::string type = (i < (int)m_level->tileHitsounds.size() && !m_level->tileHitsounds[i].empty())
            ? m_level->tileHitsounds[i] : defaultType;
        if (!m_forceHitsoundType.empty()) type = m_forceHitsoundType;
        float vol = defaultVol;

        auto key = std::make_pair(type, vol);
        auto it = groupMap.find(key);
        if (it == groupMap.end()) {
            groupMap[key] = groups.size();
            groups.push_back({type, vol, {}});
        }
        groups[groupMap[key]].timestamps.push_back(m_tileStartTimes[i] + offsetSec);
    }
    return groups;
}

int PlaybackEngine::findTileIndex(float t) const {
    int n = (int)m_tileStartTimes.size();
    if (n < 2) return 0;
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (m_tileStartTimes[mid] <= t) lo = mid + 1;
        else hi = mid - 1;
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

    // Before first tile: orbit backward at constant BPM (for long offsets)
    // Uses double precision to match computePositionsAtTime output exactly
    if (t < m_tileStartTimes[0]) {
        const auto& p0 = tiles[0].position;
        double bpm = m_tileBPM[0];
        bool cw = m_tileIsCW[0];
        double startAngle = (m_level->settings.rotation + 180.0) * 3.14159265358979 / 180.0;
        double dt = m_tileStartTimes[0] - (double)t;
        double rps = (bpm / 60.0) * 3.14159265358979;
        double angle = cw ? (startAngle + dt * rps) : (startAngle - dt * rps);
        double dist = 1.0;
        bool isRed = (0 % 2 == 0);
        if (isRed) {
            m_redPlanet->position    = glm::vec3((float)p0[0], (float)p0[1], 9.5f);
            m_bluePlanet->position   = glm::vec3((float)(p0[0] + std::cos(angle) * dist), (float)(p0[1] + std::sin(angle) * dist), 9.5f);
        } else {
            m_bluePlanet->position   = glm::vec3((float)p0[0], (float)p0[1], 9.5f);
            m_redPlanet->position    = glm::vec3((float)(p0[0] + std::cos(angle) * dist), (float)(p0[1] + std::sin(angle) * dist), 9.5f);
        }
        return;
    }

    if (tileIdx >= n - 1) {
        if (!m_reportedEnd) {
            m_reportedEnd = true;
            double wallNow = glfwGetTime();
            LOG_D("Planet reached end: tileTime=%.3fs wallTime=%.3fs wallElapsed=%.3fs",
                  t, wallNow, wallNow - m_startWallClock);
        }
        int lastIdx = n - 1;
        const auto& pivotPos = tiles[lastIdx].position;
        float bpm = m_tileBPM[lastIdx];
        bool cw = m_tileIsCW[lastIdx];

        float startAngle = 0.0f;
        if (lastIdx > 0) {
            const auto& prevPos = tiles[lastIdx - 1].position;
            startAngle = std::atan2(prevPos[1] - pivotPos[1], prevPos[0] - pivotPos[0]);
        }

        float extraTime = t - m_tileStartTimes[lastIdx];
        float radiansPerSec = (bpm / 60.0f) * 3.14159265f;
        float extraAngle = extraTime * radiansPerSec;
        float currentAngle = cw ? (startAngle - extraAngle) : (startAngle + extraAngle);
        float dist = 1.0f;

        bool isRedPivot = (lastIdx % 2 == 0);
        Planet* pivotPlanet  = isRedPivot ? m_redPlanet.get() : m_bluePlanet.get();
        Planet* movingPlanet = isRedPivot ? m_bluePlanet.get() : m_redPlanet.get();

        if (pivotPlanet)  pivotPlanet->position = glm::vec3(pivotPos[0], pivotPos[1], 9.5f);
        if (movingPlanet) movingPlanet->position = glm::vec3(pivotPos[0] + std::cos(currentAngle)*dist, pivotPos[1] + std::sin(currentAngle)*dist, 9.5f);
        return;
    }

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

    if (pivotPlanet)  pivotPlanet->position = glm::vec3(pivotPos[0], pivotPos[1], 9.5f);
    if (movingPlanet) movingPlanet->position = glm::vec3(pivotPos[0]+std::cos(currentAngle)*currentDist, pivotPos[1]+std::sin(currentAngle)*currentDist, 9.5f);
}

void PlaybackEngine::computePositionsAtTime(float t, glm::dvec2& redOut, glm::dvec2& blueOut) const {
    const auto& tiles = m_level->tiles;
    int n = (int)tiles.size();
    if (n < 2) return;
    int tileIdx = findTileIndex(t);

    // Before first tile: orbit backward at constant BPM
    if (t < m_tileStartTimes[0]) {
        const auto& p0 = tiles[0].position;
        double bpm = m_tileBPM[0];
        bool cw = m_tileIsCW[0];
        double startAngle = (m_level->settings.rotation + 180.0) * 3.14159265358979 / 180.0;
        double dt = m_tileStartTimes[0] - (double)t;
        double rps = (bpm / 60.0) * 3.14159265358979;
        double angle = cw ? (startAngle + dt * rps) : (startAngle - dt * rps);
        double dist = 1.0;
        glm::dvec2 mv(p0[0] + std::cos(angle) * dist, p0[1] + std::sin(angle) * dist);
        glm::dvec2 pv(p0[0], p0[1]);
        if (0 % 2 == 0) { redOut = pv; blueOut = mv; }
        else            { blueOut = pv; redOut = mv; }
        return;
    }

    if (tileIdx >= n - 1) {
        int lastIdx = n - 1;
        const auto& pivotPos = tiles[lastIdx].position;
        double startAngle = 0.0;
        if (lastIdx > 0) {
            const auto& prevPos = tiles[lastIdx - 1].position;
            startAngle = std::atan2(prevPos[1]-pivotPos[1], prevPos[0]-pivotPos[0]);
        }
        double extraTime = (double)(t - m_tileStartTimes[lastIdx]);
        double rps = (double)(m_tileBPM[lastIdx]/60.0) * 3.14159265358979;
        double currentAngle = m_tileIsCW[lastIdx] ? (startAngle-extraTime*rps) : (startAngle+extraTime*rps);
        glm::dvec2 mv(pivotPos[0]+std::cos(currentAngle), pivotPos[1]+std::sin(currentAngle));
        if (lastIdx%2==0) { redOut=glm::dvec2(pivotPos[0],pivotPos[1]); blueOut=mv; }
        else              { blueOut=glm::dvec2(pivotPos[0],pivotPos[1]); redOut=mv; }
        return;
    }

    bool isRed = (tileIdx%2==0);
    const auto& pivotPos = tiles[tileIdx].position;
    double startTime = m_tileStartTimes[tileIdx];
    double duration = m_tileDurations[tileIdx];
    double progress = (duration>0.0001)?((double)t-startTime)/duration:1.0;
    if (progress<0) progress=0; if (progress>1) progress=1;
    double angle = (double)m_tileStartAngles[tileIdx]+(double)m_tileTotalAngles[tileIdx]*progress;
    double dist = (double)m_tileStartDist[tileIdx]+((double)m_tileEndDist[tileIdx]-(double)m_tileStartDist[tileIdx])*progress;
    glm::dvec2 pv(pivotPos[0],pivotPos[1]);
    glm::dvec2 mv(pivotPos[0]+std::cos(angle)*dist, pivotPos[1]+std::sin(angle)*dist);
    if (isRed) { redOut=pv; blueOut=mv; }
    else       { blueOut=pv; redOut=mv; }
}

void PlaybackEngine::computePositionsAtTimeTile(float t, int tileIdx, glm::dvec2& redOut, glm::dvec2& blueOut) const {
    const auto& tiles = m_level->tiles;
    int n = (int)tiles.size();
    if (n < 2) return;
    if (t < m_tileStartTimes[0]) {
        const auto& p0 = tiles[0].position;
        double bpm = m_tileBPM[0]; bool cw = m_tileIsCW[0];
        double startAngle = (m_level->settings.rotation + 180.0) * 3.14159265358979 / 180.0;
        double dts = m_tileStartTimes[0] - (double)t;
        double rps = (bpm / 60.0) * 3.14159265358979;
        double angle = cw ? (startAngle + dts * rps) : (startAngle - dts * rps);
        glm::dvec2 mv(p0[0] + std::cos(angle), p0[1] + std::sin(angle));
        glm::dvec2 pv(p0[0], p0[1]);
        if (0 % 2 == 0) { redOut = pv; blueOut = mv; }
        else            { blueOut = pv; redOut = mv; }
        return;
    }
    if (tileIdx >= n - 1) {
        int lastIdx = n - 1;
        const auto& pivotPos = tiles[lastIdx].position;
        double startAngle = 0.0;
        if (lastIdx > 0) {
            const auto& prevPos = tiles[lastIdx - 1].position;
            startAngle = std::atan2(prevPos[1]-pivotPos[1], prevPos[0]-pivotPos[0]);
        }
        double extraTime = (double)(t - m_tileStartTimes[lastIdx]);
        double rps = (double)(m_tileBPM[lastIdx]/60.0) * 3.14159265358979;
        double currentAngle = m_tileIsCW[lastIdx] ? (startAngle-extraTime*rps) : (startAngle+extraTime*rps);
        glm::dvec2 mv(pivotPos[0]+std::cos(currentAngle), pivotPos[1]+std::sin(currentAngle));
        if (lastIdx%2==0) { redOut=glm::dvec2(pivotPos[0],pivotPos[1]); blueOut=mv; }
        else              { blueOut=glm::dvec2(pivotPos[0],pivotPos[1]); redOut=mv; }
        return;
    }
    bool isRed = (tileIdx%2==0);
    const auto& pivotPos = tiles[tileIdx].position;
    double startTime = m_tileStartTimes[tileIdx];
    double duration = m_tileDurations[tileIdx];
    double progress = (duration>0.0001)?((double)t-startTime)/duration:1.0;
    if (progress<0) progress=0; if (progress>1) progress=1;
    double angle = (double)m_tileStartAngles[tileIdx]+(double)m_tileTotalAngles[tileIdx]*progress;
    double dist = (double)m_tileStartDist[tileIdx]+((double)m_tileEndDist[tileIdx]-(double)m_tileStartDist[tileIdx])*progress;
    glm::dvec2 pv(pivotPos[0],pivotPos[1]);
    glm::dvec2 mv(pivotPos[0]+std::cos(angle)*dist, pivotPos[1]+std::sin(angle)*dist);
    if (isRed) { redOut=pv; blueOut=mv; }
    else       { blueOut=pv; redOut=mv; }
}

void PlaybackEngine::computePlanetTrails() const {
    if (!m_redPlanet || !m_bluePlanet) return;
    if (!m_redPlanet->trail || !m_bluePlanet->trail) return;

    const int maxSamples = (int)std::ceil(m_trailDuration * m_trailSampleRate) + 1;
    if (m_trailSampleRate <= 0.0f || m_trailDuration <= 0.0f) return;
    const float dt = 1.0f / m_trailSampleRate;

    float t = timeInLevel();
    std::vector<double> redXY(maxSamples*2 + 2), blueXY(maxSamples*2 + 2);
    int samples = 0;

    float startTime = t - m_trailDuration;
    int tileIdx = findTileIndex(startTime);
    if (tileIdx < 0) tileIdx = 0;
    int tsz = (int)m_tileStartTimes.size();

    for (int i = 0; i < maxSamples; i++) {
        float tt = startTime + dt * (float)i;
        if (tt > t) break;
        while (tileIdx + 1 < tsz && tt >= m_tileStartTimes[tileIdx + 1])
            tileIdx++;
        glm::dvec2 r(0), b(0);
        computePositionsAtTimeTile(tt, tileIdx, r, b);
        redXY[samples*2]=r.x; redXY[samples*2+1]=r.y;
        blueXY[samples*2]=b.x; blueXY[samples*2+1]=b.y;
        samples++;
    }
    {
        while (tileIdx + 1 < tsz && t >= m_tileStartTimes[tileIdx + 1])
            tileIdx++;
        glm::dvec2 r(0), b(0);
        computePositionsAtTimeTile(t, tileIdx, r, b);
        redXY[samples*2]=r.x; redXY[samples*2+1]=r.y;
        blueXY[samples*2]=b.x; blueXY[samples*2+1]=b.y;
        samples++;
    }
    int maxPoints = (int)std::ceil(m_trailDuration * m_trailSampleRate) + 1;
    m_redPlanet->setTrailPoints(redXY.data(), samples, maxPoints);
    m_bluePlanet->setTrailPoints(blueXY.data(), samples, maxPoints);
}

void PlaybackEngine::syncToAudio(float audioPosSec, float offsetSec) {
    if (!m_isPlaying) return;
    m_elapsedTime = ((double)audioPosSec + (double)m_audioStartOffset) * 1000.0;
    if (m_elapsedTime < 0.0) m_elapsedTime = 0.0;
    updatePlanetPositions();
    computePlanetTrails();
}

void PlaybackEngine::updateWallClock(double wallClockSec) {
    if (!m_isPlaying) return;
    m_elapsedTime = (wallClockSec - m_startWallClock) * 1000.0;
    if (m_elapsedTime < 0.0f) m_elapsedTime = 0.0;
    updatePlanetPositions();
    computePlanetTrails();
}

void PlaybackEngine::update(float deltaMs) {
    if (!m_isPlaying) return;
    m_elapsedTime += deltaMs;
    updatePlanetPositions();
    computePlanetTrails();
}
