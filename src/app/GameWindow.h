#pragma once

#include "LauncherWindow.h"
#include "../level/LevelData.h"
#include <memory>

void showGameWindow(const LauncherConfig& cfg, std::unique_ptr<LevelData> level);
