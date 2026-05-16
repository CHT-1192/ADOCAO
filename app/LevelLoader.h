#pragma once

#include "LoadingWindow.h"
#include "LauncherWindow.h"
#include "level/LevelData.h"
#include "game/PlaybackEngine.h"
#include "audio/HitsoundManager.h"
#include "audio/AudioEngine.h"
#include <memory>

struct LoadResult {
    std::unique_ptr<LevelData> level;
    PlaybackEngine playback;
    HitsoundManager hitsounds;
    AudioEngine audio;
};

void runLevelLoading(const LauncherConfig& cfg, LoadingProgress& progress, LoadResult& result);
