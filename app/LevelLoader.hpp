#pragma once

#include "LoadingWindow.hpp"
#include "LauncherWindow.hpp"
#include "level/LevelData.hpp"
#include "game/PlaybackEngine.hpp"
#include "audio/HitsoundManager.hpp"
#include "audio/AudioEngine.hpp"
#include <memory>

struct LoadResult {
    std::shared_ptr<LevelData> level;
    std::shared_ptr<PlaybackEngine> playback;
    HitsoundManager hitsounds;
    AudioEngine audio;
};

// Wizard preload step (after "Next"): parse level + precalculate timing.
// Does NOT load audio or synthesize hitsounds (those happen after Start).
void runLevelPreload(const LauncherConfig& cfg, LoadingProgress& progress,
                     std::shared_ptr<LevelData>& outLevel,
                     std::shared_ptr<PlaybackEngine>& outPlayback);

// Full load used after the launcher. If cfg.preloadedLevel/Playback are set
// (wizard preload done), reuses them and only loads audio + synthesizes
// hitsounds; otherwise does the whole load (CLI mode).
void runLevelLoading(const LauncherConfig& cfg, LoadingProgress& progress, LoadResult& result);
