#pragma once

#include "LoadingWindow.hpp"
#include "LauncherWindow.hpp"
#include "core/level/LevelData.hpp"
#include "core/timeline/Timeline.hpp"
#include "core/timeline/PlaybackClock.hpp"
#include "audio/HitsoundManager.hpp"
#include "audio/AudioEngine.hpp"
#include <memory>

struct LoadResult {
    std::shared_ptr<LevelData> level;
    std::shared_ptr<Timeline> timeline;
    std::shared_ptr<PlaybackClock> playback;
    HitsoundManager hitsounds;
    AudioEngine audio;
};

// Wizard preload step (after "Next"): parse level + precalculate timing.
// Does NOT load audio or synthesize hitsounds (those happen after Start).
void runLevelPreload(const LauncherConfig& cfg, LoadingProgress& progress,
                     std::shared_ptr<LevelData>& outLevel,
                     std::shared_ptr<Timeline>& outTimeline);

// Full load used after the launcher. If cfg.preloadedLevel/preloadedTimeline
// are set (wizard preload done), reuses them and only loads audio + synthesizes
// hitsounds; otherwise does the whole load (CLI mode).
void runLevelLoading(const LauncherConfig& cfg, LoadingProgress& progress, LoadResult& result);
