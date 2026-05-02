#pragma once

#include <string>
#include <vector>
#include <map>
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

    bool loadFromFile(const std::string& filepath);
    bool loadFromString(const std::string& jsonStr);

private:
    void calculateTilePositions();
    void convertPathToAngles();
    static float pathCharToAngle(char c);
};
