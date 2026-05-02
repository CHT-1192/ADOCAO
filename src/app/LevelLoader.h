#pragma once

#include "LoadingWindow.h"
#include "LauncherWindow.h"
#include "../level/LevelData.h"
#include <memory>

// Orchestrates the multi-step loading process, reporting progress.
// Takes ownership of parsed level data.
std::unique_ptr<LevelData> runLevelLoading(
    const LauncherConfig& cfg,
    LoadingProgress& progress);
