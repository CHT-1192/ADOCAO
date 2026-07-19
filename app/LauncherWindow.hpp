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
    std::string forceHitsoundType;  // force "None" hitsound to this type (empty = disabled)
    bool   autoPlay = false;       // auto-start playback after loading
    bool   legacyCulling = false;  // use legacy brute-force culling
    int    msaaSamples = 0;        // MSAA samples (0=off, 2, 4, 8)
    int  resolutionW = 1920;
    int  resolutionH = 1080;
    bool fullscreen = false;
    bool showTrail = true;
    bool exportHitsounds = false;
    bool cancelled = false;
};

// Opens a centered ImGui launcher window. Returns config after user clicks Start or closes.
LauncherConfig showLauncher();
