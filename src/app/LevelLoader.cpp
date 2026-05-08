#include "LevelLoader.h"
#include <cstring>
#include <thread>
#include <future>

#ifdef _WIN32
#include <windows.h>
#endif

static void report(LoadingProgress& p, float pct, const char* text) {
    p.stage++;
    p.percent.store(pct);
    strncpy(p.stageText, text, sizeof(p.stageText) - 1);
    p.stageText[sizeof(p.stageText) - 1] = '\0';
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

    // ---- Phase 1: Parse level (2-45%) ----
    result.level = std::make_unique<LevelData>();
    auto onParseProgress = [&](float pct, const char* stage) {
        report(progress, 0.02f + pct * 0.43f, stage);
    };
    if (!result.level->loadFromFile(cfg.levelPath, onParseProgress)) {
        report(progress, 0.0f, "Error: Failed to parse level");
        result.level.reset();
        return;
    }

    // ---- Phase 2: Precalculate timing (45-75%) ----
    report(progress, 0.45f, "Precalculating timeline...");
    result.playback.init(*result.level, cfg.showTrail);

    // Wait for audio init to finish
    if (audioFuture.valid()) {
        report(progress, 0.75f, "Finalizing audio...");
        audioFuture.get();
    } else {
        report(progress, 0.75f, "Initializing audio...");
        result.audio.init();
    }

    report(progress, 0.80f, "Pre-synthesizing hitsounds...");
    result.hitsounds.init();
    result.hitsounds.setHitsoundType(result.level->settings.hitsound);
    result.hitsounds.setVolume(result.level->settings.hitsoundVolume);

    auto timestamps = result.playback.getHitsoundTimestamps();
    float duration = result.playback.totalDuration();
    result.hitsounds.preSynthesize(timestamps, duration,
        [&](float pct) {
            report(progress, 0.80f + pct * 0.19f, "Synthesizing hitsounds...");
        });

    report(progress, 1.0f, "Ready!");
}
