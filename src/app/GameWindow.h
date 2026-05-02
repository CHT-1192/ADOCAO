#pragma once

#include "LauncherWindow.h"
#include "../level/LevelData.h"
#include <memory>

// Opens the main game window (pure OpenGL, no ImGui).
// Runs the game loop. Returns when user closes the window.
void showGameWindow(const LauncherConfig& cfg, std::unique_ptr<LevelData> level);
