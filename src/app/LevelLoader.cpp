#include "LevelLoader.h"
#include <cstring>

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
    report(progress, 0.02f, "Parsing level file...");

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

    // ---- Phase 3: Audio setup (75-95%) ----
    report(progress, 0.75f, "Initializing audio...");
    if (!result.audio.init()) {
        report(progress, 0.90f, "Audio init failed, continuing...");
    } else if (!cfg.musicPath.empty()) {
        report(progress, 0.80f, "Loading music...");
        result.audio.loadMusic(cfg.musicPath);
    }

    report(progress, 0.82f, "Pre-synthesizing hitsounds...");
    result.hitsounds.init();
    result.hitsounds.setHitsoundType(result.level->settings.hitsound);
    result.hitsounds.setVolume(result.level->settings.hitsoundVolume);

    auto timestamps = result.playback.getHitsoundTimestamps();
    float duration = result.playback.totalDuration();
    result.hitsounds.preSynthesize(timestamps, duration,
        [&](float pct) {
            report(progress, 0.82f + pct * 0.16f, "Synthesizing hitsounds...");
        });

    report(progress, 0.99f, "Finalizing...");
    report(progress, 1.0f, "Ready!");
}
