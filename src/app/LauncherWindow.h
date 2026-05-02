#pragma once

#include <string>
#include <functional>

// User selections from the launcher
struct LauncherConfig {
    std::string levelPath;
    std::string musicPath;
    int  resolutionW = 1920;
    int  resolutionH = 1080;
    bool fullscreen = false;
    bool cancelled = false;   // user closed the window without Start
};

// Opens a centered ImGui launcher window. Returns config after user clicks Start or closes.
LauncherConfig showLauncher();
