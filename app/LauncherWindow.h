#pragma once

#include <string>
#include <functional>

// User selections from the launcher
struct LauncherConfig {
    std::string levelPath;
    std::string musicPath;
    std::string trackFillColor   = "DEBB7B";  // 6-char hex
    std::string trackStrokeColor = "6F5D3D";  // 6-char hex (DEBB7B * 0.5)
    std::string backgroundColor  = "000000";  // 6-char hex
    bool   autoStroke   = true;
    bool   enableHitsounds = true;
    bool   forceHitsounds = false;  // force "None" hitsound → "Kick"
    int  resolutionW = 1280;
    int  resolutionH = 720;
    bool fullscreen = false;
    bool showTrail = true;
    bool exportHitsounds = false;
    bool cancelled = false;
};

// Opens a centered ImGui launcher window. Returns config after user clicks Start or closes.
LauncherConfig showLauncher();
