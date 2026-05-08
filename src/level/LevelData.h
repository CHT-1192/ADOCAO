#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <nlohmann/json.hpp>

// Parsed .adofai level file
struct LevelData {
    struct Settings {
        int    version = 15;
        float  bpm = 100.0f;
        float  offset = 0.0f;        // ms
        int    countdownTicks = 4;
        float  zoom = 100.0f;
        float  rotation = 0.0f;
        std::string relativeTo = "Player";
        std::array<float, 2> position = {0.0f, 0.0f};
        std::string hitsound = "Kick";
        float  hitsoundVolume = 100.0f;
        std::string trackColor = "debb7b";
        std::string secondaryTrackColor = "ffffff";
        std::string backgroundColor = "000000";
        bool   stickToFloors = true;
        std::string planetEase = "Linear";
        // ... more fields as needed
    };

    struct Tile {
        int   index = 0;
        float angle = 180.0f;       // degrees
        float direction = 0.0f;     // outgoing angle (degrees)
        std::array<float, 2> position = {0.0f, 0.0f};
    };

    Settings settings;
    std::vector<float> angleData;
    std::string        pathData;       // raw pathData string (alternative to angleData)
    std::vector<Tile>  tiles;
    nlohmann::json     actions;       // raw JSON array
    nlohmann::json     decorations;   // raw JSON array

    struct TilePositionOffset {
        float offsetX = 0.0f;
        float offsetY = 0.0f;
        bool  justThisTile = false;
    };

    // Per-tile event data (computed from actions)
    std::vector<float> tileBPMs;      // BPM for each tile (after SetSpeed events)
    std::vector<bool>  tileHasTwirl;  // true if tile has a Twirl event
    std::vector<bool>  tileHasSetSpeed; // true if tile has a SetSpeed event
    std::vector<TilePositionOffset> tilePositionOffsets;

    using ProgressCb = std::function<void(float pct, const char* stage)>;

    bool loadFromFile(const std::string& filepath, ProgressCb onProgress = nullptr);
    bool loadFromString(const std::string& jsonStr, ProgressCb onProgress = nullptr);

private:
    void calculateTilePositions();
    void convertPathToAngles();
    void processActions();
    void applyPositionTrackOffsets();
    static float pathCharToAngle(char c);
};
