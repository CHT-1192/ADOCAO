#pragma once

#include "LoadingWindow.hpp"
#include "LauncherWindow.hpp"
#include "level/LevelData.hpp"
#include "game/PlaybackEngine.hpp"
#include "audio/HitsoundManager.hpp"
#include "audio/AudioEngine.hpp"
#include <memory>

struct LoadResult {
    std::unique_ptr<LevelData> level;
    PlaybackEngine playback;
    HitsoundManager hitsounds;
    AudioEngine audio;
};

void runLevelLoading(const LauncherConfig& cfg, LoadingProgress& progress, LoadResult& result);
