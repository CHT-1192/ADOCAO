#include "LevelLoader.hpp"
#include <cstring>
#include <thread>
#include <future>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#endif

static void report(LoadingProgress& p, float pct, const char* text) {
    p.stage++;
    p.percent.store(pct);
    {
        std::lock_guard<std::mutex> lock(p.textMutex);
        strncpy(p.stageText, text, sizeof(p.stageText) - 1);
        p.stageText[sizeof(p.stageText) - 1] = '\0';
    }
}

void runLevelPreload(const LauncherConfig& cfg, LoadingProgress& progress,
                     std::shared_ptr<LevelData>& outLevel,
                     std::shared_ptr<PlaybackEngine>& outPlayback) {
    // Phase 1: parse level
    outLevel = std::make_shared<LevelData>();
    auto onParseProgress = [&](float pct, const char* stage) {
        report(progress, 0.05f + pct * 0.60f, stage);
    };
    if (!outLevel->loadFromFile(cfg.levelPath, onParseProgress)) {
        report(progress, 0.0f, "Error: Failed to parse level");
        outLevel.reset();
        return;
    }

    // Phase 2: precalculate timing (tile positions / timeline)
    report(progress, 0.70f, "Precalculating timeline...");
    outPlayback = std::make_shared<PlaybackEngine>();
    outPlayback->init(*outLevel, true);  // trail final value applied after Start
    report(progress, 1.0f, "Preload complete");
}

void runLevelLoading(const LauncherConfig& cfg, LoadingProgress& progress, LoadResult& result) {
    report(progress, 0.02f, "Starting audio engine...");

    // Start audio init + music loading in background immediately (parallel with level parsing)
    std::future<void> audioFuture;
    if (!cfg.musicPath.empty()) {
        audioFuture = std::async(std::launch::async, [&]() {
            result.audio.init();
            result.audio.loadMusic(cfg.musicPath);
        });
    } else {
        result.audio.init();
    }

    if (!cfg.preloadedLevel || !cfg.preloadedPlayback) {
        // Full path (CLI mode): parse level + precalculate timing
        report(progress, 0.05f, "Parsing level...");
        result.level = std::make_shared<LevelData>();
        auto onParseProgress = [&](float pct, const char* stage) {
            report(progress, 0.05f + pct * 0.40f, stage);
        };
        if (!result.level->loadFromFile(cfg.levelPath, onParseProgress)) {
            report(progress, 0.0f, "Error: Failed to parse level");
            result.level.reset();
            return;
        }
        report(progress, 0.45f, "Precalculating timeline...");
        result.playback = std::make_shared<PlaybackEngine>();
        result.playback->init(*result.level, cfg.showTrail);
    } else {
        // Wizard preload already parsed level + computed timeline
        result.level  = cfg.preloadedLevel;
        result.playback = cfg.preloadedPlayback;
        report(progress, 0.50f, "Applying final settings...");
    }

    // Final settings (may have been changed in the wizard after the preload)
    result.playback->setForceHitsoundType(cfg.forceHitsoundType);
    result.playback->setLagacyCulling(cfg.legacyCulling);
    result.playback->setTrailDuration(cfg.trailDuration);
    result.playback->setTrailSampleRate(cfg.trailSampleRate);

    // Wait for background audio init to finish (if started)
    if (audioFuture.valid()) {
        report(progress, 0.75f, "Finalizing audio...");
        audioFuture.get();
    }

    report(progress, 0.80f, "Synthesizing hitsounds...");
    if (cfg.enableHitsounds) {
        result.hitsounds.init();
        result.hitsounds.preSynthesize(result.playback->getHitsoundTimestampGroups(),
                                       result.playback->totalDuration());
    }

    // Release data no longer needed (angleData, actions, position offsets)
    result.level->releaseMemory();

    report(progress, 1.0f, "Ready!");
}
