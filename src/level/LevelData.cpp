#include "LevelData.h"
#include "../util/Logger.h"
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#endif

static std::string cleanJson(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());

    bool inString = false;
    bool escaped = false;
    char lastOut = 0;  // last non-whitespace char written to output

    for (size_t i = 0; i < raw.size(); i++) {
        char c = raw[i];

        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            if (c == '\r') continue;
            lastOut = c;
            out.push_back(c);
            continue;
        }

        if (c == '"') {
            inString = true;
            // Insert missing comma: ...}" or ...]" or ...""  →  ...,"
            if (lastOut == '}' || lastOut == ']' || lastOut == '"' || (lastOut >= '0' && lastOut <= '9')) {
                out.push_back(',');
            }
            lastOut = '"';
            out.push_back(c);
            continue;
        }

        if (c == '\r') continue;

        // Insert missing comma: ...}{...  or  ...]{...  or  ..."[...
        if ((c == '{' || c == '[') && (lastOut == '}' || lastOut == ']' || lastOut == '"' || (lastOut >= '0' && lastOut <= '9'))) {
            out.push_back(',');
        }

        // Remove trailing comma: ",\n  }" or ",\n  ]"
        if (c == ',') {
            size_t j = i + 1;
            while (j < raw.size() && (raw[j] == ' ' || raw[j] == '\t' || raw[j] == '\n')) j++;
            if (j < raw.size() && (raw[j] == '}' || raw[j] == ']')) continue;
        }

        if (c != ' ' && c != '\t' && c != '\n') lastOut = c;
        out.push_back(c);
    }

    return out;
}

// ADOFAI uses both booleans and "Enabled"/"Disabled" strings for bool fields
static bool parseBool(const nlohmann::json& obj, const char* key, bool def = false) {
    if (!obj.contains(key)) return def;
    auto& v = obj[key];
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_string()) {
        std::string s = v.get<std::string>();
        return s == "Enabled" || s == "true";
    }
    return def;
}

static std::string readFileUtf8(const std::string& filepath) {
#ifdef _WIN32
    // Convert UTF-8 → UTF-16 and use _wfopen for Unicode paths
    int wlen = MultiByteToWideChar(CP_UTF8, 0, filepath.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return {};
    std::wstring wpath(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, filepath.c_str(), -1, &wpath[0], wlen);

    FILE* f = _wfopen(wpath.c_str(), L"rb");
    if (!f) return {};

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::string content(size, '\0');
    fread(&content[0], 1, size, f);
    fclose(f);
    return content;
#else
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) return {};
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
#endif
}

bool LevelData::loadFromFile(const std::string& filepath) {
    // Debug: hex dump path bytes
    {
        std::string hex;
        for (unsigned char c : filepath) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%02X ", c);
            hex += buf;
        }
        LOG_I("Path hex: %s", hex.c_str());
    }

    std::string content = readFileUtf8(filepath);
    if (content.empty()) {
        LOG_E("Cannot open level file: %s", filepath.c_str());
        return false;
    }
    return loadFromString(cleanJson(content));
}

bool LevelData::loadFromString(const std::string& jsonStr) {
    try {
        auto root = nlohmann::json::parse(cleanJson(jsonStr));

        // angleData
        if (root.contains("angleData") && root["angleData"].is_array()) {
            angleData = root["angleData"].get<std::vector<float>>();
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

        // Convert pathData → angleData if needed
        if (!pathData.empty() && angleData.empty()) {
            convertPathToAngles();
        }

        calculateTilePositions();
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
        if (angleData[i] == 999.0f) {
            floats[i] = (i > 0 ? floats[i - 1] : 0.0f) + 180.0f;
        } else {
            floats[i] = angleData[i];
        }
    }

    tiles.resize(n);
    float curX = 0.0f, curY = 0.0f;

    for (int i = 0; i < n; i++) {
        tiles[i].index = i;
        tiles[i].position = {curX, curY};
        tiles[i].direction = floats[i];  // outgoing angle to next tile

        float rad = floats[i] * 3.14159265f / 180.0f;
        curX += std::cos(rad);
        curY += std::sin(rad);
    }

    // Append extra tile (infinite rotation reference)
    if (n > 0) {
        Tile extra;
        extra.index = n;
        float dir = 0.0f;
        // Use direction of last segment if possible
        if (n > 1) {
            float dx = tiles[n-1].position[0] - tiles[n-2].position[0];
            float dy = tiles[n-1].position[1] - tiles[n-2].position[1];
            float dist = std::sqrt(dx*dx + dy*dy);
            if (dist > 0.01f) {
                dir = std::atan2(dy, dx) * 180.0f / 3.14159265f;
            }
        }
        float rad = dir * 3.14159265f / 180.0f;
        float length = 1.0f;
        // Use last segment distance
        if (n > 1) {
            float dx = tiles[n-1].position[0] - tiles[n-2].position[0];
            float dy = tiles[n-1].position[1] - tiles[n-2].position[1];
            length = std::sqrt(dx*dx + dy*dy);
            if (length < 0.01f) length = 1.0f;
        }
        extra.position = {
            tiles[n-1].position[0] + std::cos(rad) * length,
            tiles[n-1].position[1] + std::sin(rad) * length
        };
        extra.angle = 180.0f;
        extra.direction = dir;
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

void LevelData::convertPathToAngles() {
    if (pathData.empty()) return;

    angleData.clear();
    angleData.reserve(pathData.size());

    for (char c : pathData) {
        angleData.push_back(pathCharToAngle(c));
    }
}
