#include "LevelData.h"
#include "JsonCleaner.h"
#include "util/Logger.h"
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#endif

// Fast parser: extract angleData float array from JSON without DOM allocation.
// Returns the parsed array and writes the end position (after ']') to `outArrayEnd`.
static std::vector<double> parseAngleDataFast(const char* json, size_t len, size_t& outArrayEnd) {
    const char* key = "\"angleData\"";
    const char* pos = (const char*)std::memchr(json, '"', len);
    while (pos) {
        size_t remain = len - (pos - json);
        if (remain >= 11 && std::memcmp(pos, key, 11) == 0) {
            pos += 11;
            while (pos < json + len && (*pos == ' ' || *pos == ':' || *pos == '\t' || *pos == '\n'))
                pos++;
            if (*pos != '[') return {};
            pos++; // skip '['
            break;
        }
        pos++;
        pos = (const char*)std::memchr(pos, '"', json + len - pos);
    }
    if (!pos) return {};

    std::vector<double> result;
    while (pos < json + len) {
        while (pos < json + len && (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r'))
            pos++;
        if (pos >= json + len) break;
        if (*pos == ']') { outArrayEnd = pos - json + 1; break; }
        if (*pos == ',') { pos++; continue; }
        char* end;
        double val = strtod(pos, &end);
        if (end == pos) { pos++; continue; }
        result.push_back(val);
        pos = end;
    }
    return result;
}

static std::string readFileUtf8(const std::string& filepath) {
#ifdef _WIN32
    // Convert UTF-8 path to wide for Windows API
    int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                   filepath.c_str(), -1, nullptr, 0);
    std::wstring wpath;
    if (wlen > 0) {
        wpath.resize(wlen);
        MultiByteToWideChar(CP_UTF8, 0, filepath.c_str(), -1, &wpath[0], wlen);
    } else {
        wlen = MultiByteToWideChar(CP_ACP, 0, filepath.c_str(), -1, nullptr, 0);
        if (wlen <= 0) return {};
        wpath.resize(wlen);
        MultiByteToWideChar(CP_ACP, 0, filepath.c_str(), -1, &wpath[0], wlen);
    }

    // Memory-mapped file: avoids OS buffer copy for large files
    HANDLE hFile = CreateFileW(wpath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return {};

    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart <= 0) {
        CloseHandle(hFile); return {};
    }

    HANDLE hMapping = CreateFileMappingW(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!hMapping) { CloseHandle(hFile); return {}; }

    const char* data = (const char*)MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
    if (!data) { CloseHandle(hMapping); CloseHandle(hFile); return {}; }

    size_t size = (size_t)fileSize.QuadPart;
    // Skip UTF-8 BOM if present
    size_t offset = (size >= 3 && (unsigned char)data[0] == 0xEF
                     && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF) ? 3 : 0;

    std::string content(data + offset, size - offset);

    UnmapViewOfFile(data);
    CloseHandle(hMapping);
    CloseHandle(hFile);
    return content;
#else
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) return {};
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
#endif
}

bool LevelData::loadFromFile(const std::string& filepath, ProgressCb onProgress) {
    if (onProgress) onProgress(0.05f, "Reading file...");
    std::string content = readFileUtf8(filepath);
    if (content.empty()) {
        LOG_E("Cannot open level file: %s", filepath.c_str());
        return false;
    }
    return loadFromString(cleanJson(content), onProgress);
}

bool LevelData::loadFromString(const std::string& jsonStr, ProgressCb onProgress) {
    try {
        if (onProgress) onProgress(0.10f, "Parsing angleData...");

        // Fast path: parse angleData directly without JSON DOM allocation
        size_t angleDataEnd = 0;
        angleData = parseAngleDataFast(jsonStr.c_str(), jsonStr.size(), angleDataEnd);

        // Build a stripped version: replace the angleData array with [] so
        // nlohmann only parses the small objects (settings, actions, etc.)
        std::string stripped;
        if (angleDataEnd > 0) {
            // Find the start of the angleData array
            const char* p = std::strstr(jsonStr.c_str(), "\"angleData\"");
            size_t arrStart = p ? (p - jsonStr.c_str()) + 11 : 0;
            // Skip whitespace and colon
            while (arrStart < jsonStr.size() && (jsonStr[arrStart] == ' ' || jsonStr[arrStart] == ':'
                   || jsonStr[arrStart] == '\t' || jsonStr[arrStart] == '\n'))
                arrStart++;
            stripped.reserve(arrStart + 2 + (jsonStr.size() - angleDataEnd));
            stripped.append(jsonStr, 0, arrStart);
            stripped += "[]";
            stripped.append(jsonStr, angleDataEnd, std::string::npos);
        } else {
            stripped = jsonStr;
        }

        if (onProgress) onProgress(0.12f, "Parsing JSON...");
        auto root = nlohmann::json::parse(stripped);

        if (onProgress) onProgress(0.15f, "Extracting level data...");
        // angleData already parsed via fast path above; fallback to nlohmann if not found
        if (angleData.empty() && root.contains("angleData") && root["angleData"].is_array()) {
            angleData = root["angleData"].get<std::vector<double>>();
        }

        // settings
        if (root.contains("settings")) {
            auto& s = root["settings"];
            settings.bpm             = s.value("bpm", 100.0f);
            settings.offset          = s.value("offset", 0.0f);
            settings.countdownTicks  = s.value("countdownTicks", 4);
            settings.zoom            = s.value("zoom", 100.0f);
            settings.rotation        = s.value("rotation", 0.0f);
            settings.relativeTo      = s.value("relativeTo", "Player");
            settings.hitsound        = s.value("hitsound", "Kick");
            settings.hitsoundVolume  = s.value("hitsoundVolume", 100.0f);
            settings.trackColor      = s.value("trackColor", "debb7b");
            settings.secondaryTrackColor = s.value("secondaryTrackColor", "ffffff");
            settings.backgroundColor = s.value("backgroundColor", "000000");
            settings.stickToFloors   = parseBool(s, "stickToFloors", true);
            settings.planetEase      = s.value("planetEase", "Linear");

            if (s.contains("position") && s["position"].is_array() && s["position"].size() >= 2) {
                settings.position = {s["position"][0].get<float>(), s["position"][1].get<float>()};
            }
        }

        // pathData (alternative to angleData)
        if (root.contains("pathData") && root["pathData"].is_string()) {
            pathData = root["pathData"].get<std::string>();
        }

        // actions
        if (root.contains("actions")) {
            actions = root["actions"];
        }

        // decorations
        if (root.contains("decorations")) {
            decorations = root["decorations"];
        }

        if (onProgress) onProgress(0.20f, "Processing level data...");

        // Convert pathData → angleData if needed
        if (!pathData.empty() && angleData.empty()) {
            convertPathToAngles();
        }

        if (onProgress) onProgress(0.30f, "Calculating tile positions...");
        calculateTilePositions();
        if (onProgress) onProgress(0.40f, "Processing actions...");
        processActions();
        applyPositionTrackOffsets();
        return true;
    } catch (const std::exception& e) {
        LOG_E("JSON parse error: %s", e.what());
        return false;
    }
}

void LevelData::calculateTilePositions() {
    tiles.clear();
    if (angleData.empty()) return;

    int n = static_cast<int>(angleData.size());

    // Build "floats" array: 999 = midspin (previous + 180)
    std::vector<float> floats(n);
    for (int i = 0; i < n; i++) {
        if (angleData[i] == 999.0) {
            floats[i] = (i > 0 ? floats[i - 1] : 0.0f) + 180.0f;
        } else {
            floats[i] = angleData[i];
        }
    }

    tiles.resize(n);
    double curX = 0.0, curY = 0.0;  // double for precision

    for (int i = 0; i < n; i++) {
        tiles[i].index = i;
        tiles[i].position = {curX, curY};
        tiles[i].direction = floats[i];

        double rad = (double)floats[i] * 3.14159265358979323846 / 180.0;
        curX += std::cos(rad);
        curY += std::sin(rad);
    }

    // Append extra tile (infinite rotation reference)
    if (n > 0) {
        Tile extra;
        extra.index = n;
        double dir = 0.0;
        if (n > 1) {
            double dx = (double)tiles[n-1].position[0] - (double)tiles[n-2].position[0];
            double dy = (double)tiles[n-1].position[1] - (double)tiles[n-2].position[1];
            double dist = std::sqrt(dx*dx + dy*dy);
            if (dist > 0.01) dir = std::atan2(dy, dx) * 180.0 / 3.14159265358979323846;
        }
        double rad = dir * 3.14159265358979323846 / 180.0;
        double length = 1.0;
        if (n > 1) {
            double dx = (double)tiles[n-1].position[0] - (double)tiles[n-2].position[0];
            double dy = (double)tiles[n-1].position[1] - (double)tiles[n-2].position[1];
            length = std::sqrt(dx*dx + dy*dy);
            if (length < 0.01) length = 1.0;
        }
        extra.position = {
            (float)(tiles[n-1].position[0] + std::cos(rad) * length),
            (float)(tiles[n-1].position[1] + std::sin(rad) * length)
        };
        extra.angle = 180.0f;
        extra.direction = (float)dir;
        tiles.push_back(extra);
    }
}

// ADOFAI pathData → angleData conversion
// Based on ADOFAI-JS official mapping table (src/pathdata/index.ts)

float LevelData::pathCharToAngle(char c) {
    switch (c) {
        case 'R': return 0;
        case 'p': return 15;
        case 'J': return 30;
        case 'E': return 45;
        case 'T': return 60;
        case 'o': return 75;
        case 'U': return 90;
        case 'q': return 105;
        case 'G': return 120;
        case 'Q': return 135;
        case 'H': return 150;
        case 'W': return 165;
        case 'L': return 180;
        case 'x': return 195;
        case 'N': return 210;
        case 'Z': return 225;
        case 'F': return 240;
        case 'V': return 255;
        case 'D': return 270;
        case 'Y': return 285;
        case 'B': return 300;
        case 'C': return 315;
        case 'M': return 330;
        case 'A': return 345;
        case '5': return 555;   // multi-hit stack 5
        case '6': return 666;   // multi-hit stack 6
        case '7': return 777;   // multi-hit stack 7
        case '8': return 888;   // multi-hit stack 8
        case '!': return 999;   // midspin
        default:  return 0;
    }
}

void LevelData::processActions() {
    int n = (int)tiles.size();
    tileBPMs.assign(n, settings.bpm);
    tileHasTwirl.assign(n, false);
    tileHasSetSpeed.assign(n, false);
    tileHitsounds.assign(n, "");
    tilePositionOffsets.assign(n, {});
    tileFillColors.assign(n, settings.trackColor);
    tileStrokeColors.assign(n, settings.secondaryTrackColor);

    if (actions.is_null() || !actions.is_array()) return;

    float currentBPM = settings.bpm;

    for (size_t i = 0; i < actions.size(); i++) {
        auto& a = actions[i];
        if (!a.is_object()) continue;
        if (!a.contains("floor") || !a.contains("eventType")) continue;

        int floor = a["floor"].get<int>();
        if (floor < 0 || floor >= n) continue;

        std::string etype = a["eventType"].get<std::string>();

        if (etype == "Twirl") {
            tileHasTwirl[floor] = true;
        } else if (etype == "SetSpeed") {
            tileHasSetSpeed[floor] = true;
            std::string stype = a.value("speedType", std::string("Bpm"));
            if (stype == "Multiplier") {
                currentBPM *= a.value("bpmMultiplier", 1.0f);
            } else {
                currentBPM = a.value("beatsPerMinute", currentBPM);
            }
        } else if (etype == "PositionTrack") {
            if (a.contains("positionOffset") && a["positionOffset"].is_array() && a["positionOffset"].size() >= 2) {
                tilePositionOffsets[floor].offsetX = a["positionOffset"][0].get<float>();
                tilePositionOffsets[floor].offsetY = a["positionOffset"][1].get<float>();
                tilePositionOffsets[floor].justThisTile = parseBool(a, "justThisTile", false);
            }
        } else if (etype == "SetHitsound") {
            std::string hs = a.value("hitsound", std::string());
            if (!hs.empty() && floor >= 0 && floor < n) {
                for (int k = floor; k < n; k++)
                    tileHitsounds[k] = hs;
            }
        } else if (etype == "ColorTrack") {
            std::string fc = a.value("trackColor", std::string());
            std::string sc = a.value("secondaryTrackColor", std::string());
            bool jtt = parseBool(a, "justThisTile", false);
            if ((!fc.empty() || !sc.empty()) && floor >= 0 && floor < n) {
                int end = jtt ? floor + 1 : n;
                for (int k = floor; k < end; k++) {
                    if (!fc.empty()) tileFillColors[k] = fc;
                    if (!sc.empty()) tileStrokeColors[k] = sc;
                }
            }
        }
    }

    // Pre-index SetSpeed events by floor (O(m) instead of O(n*m) per-tile scan)
    struct SS { float multiplier; float bpm; bool isMultiplier; };
    std::vector<SS> setSpeedByFloor(n);  // 0 = no event
    for (size_t j = 0; j < actions.size(); j++) {
        auto& a = actions[j];
        if (!a.is_object() || !a.contains("floor") || !a.contains("eventType")) continue;
        int floor = a["floor"].get<int>();
        if (floor < 0 || floor >= n) continue;
        if (a["eventType"].get<std::string>() != "SetSpeed") continue;
        std::string st = a.value("speedType", std::string("Bpm"));
        SS ev;
        ev.isMultiplier = (st == "Multiplier");
        ev.multiplier = ev.isMultiplier ? a.value("bpmMultiplier", 1.0f) : 0.0f;
        ev.bpm = ev.isMultiplier ? 0.0f : a.value("beatsPerMinute", 0.0f);
        setSpeedByFloor[floor] = ev;
    }

    // Propagate BPM forward in O(n)
    float runningBPM = settings.bpm;
    for (int i = 0; i < n; i++) {
        if (setSpeedByFloor[i].isMultiplier)
            runningBPM *= setSpeedByFloor[i].multiplier;
        else if (setSpeedByFloor[i].bpm > 0.0f)
            runningBPM = setSpeedByFloor[i].bpm;
        tileBPMs[i] = runningBPM;
    }
}

void LevelData::applyPositionTrackOffsets() {
    if (tilePositionOffsets.empty()) return;

    double cumX = 0.0, cumY = 0.0;
    int n = (int)tiles.size();

    for (int i = 0; i < n; i++) {
        if (i < (int)tilePositionOffsets.size()) {
            cumX += tilePositionOffsets[i].offsetX;
            cumY += tilePositionOffsets[i].offsetY;
        }

        tiles[i].position[0] += cumX;
        tiles[i].position[1] += cumY;

        if (i < (int)tilePositionOffsets.size() && tilePositionOffsets[i].justThisTile) {
            cumX = 0.0;
            cumY = 0.0;
        }
    }
}

void LevelData::releaseMemory() {
    // Free large arrays no longer needed after loading
    std::vector<double>().swap(angleData);
    actions = nlohmann::json();
    decorations = nlohmann::json();
    tilePositionOffsets.clear(); tilePositionOffsets.shrink_to_fit();
    // tileBPMs kept: needed by buildIcons() for SetSpeed icon coloring
    tileFillColors.clear(); tileFillColors.shrink_to_fit();
    tileStrokeColors.clear(); tileStrokeColors.shrink_to_fit();
}

void LevelData::convertPathToAngles() {
    if (pathData.empty()) return;

    angleData.clear();
    angleData.reserve(pathData.size());

    for (char c : pathData) {
        angleData.push_back(pathCharToAngle(c));
    }
}
