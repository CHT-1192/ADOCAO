#include "LevelLoader.h"
#include <thread>
#include <chrono>
#include <cstring>

#ifdef _WIN32
# include <windows.h>
#endif

static void setStage(LoadingProgress& p, int stage, float pct, const char* text) {
    p.stage = stage;
    p.percent.store(pct);
    strncpy(p.stageText, text, sizeof(p.stageText) - 1);
    p.stageText[sizeof(p.stageText) - 1] = '\0';
}

std::unique_ptr<LevelData> runLevelLoading(
    const LauncherConfig& cfg,
    LoadingProgress& progress)
{
    setStage(progress, 1, 5.0f, "Parsing level file...");

    auto level = std::make_unique<LevelData>();

    // Simulate brief step for smooth progress bar feel
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    setStage(progress, 2, 15.0f, "Parsing ADOFAI JSON...");
    if (!level->loadFromFile(cfg.levelPath)) {
        setStage(progress, -1, 0.0f, "Error: Failed to parse level file");
        return nullptr;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    setStage(progress, 3, 40.0f,
        ("Calculating tile positions (" + std::to_string(level->tiles.size()) + " tiles)...").c_str());

    setStage(progress, 4, 60.0f, "Preparing timeline data...");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    setStage(progress, 5, 70.0f, "Loading audio resources...");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    setStage(progress, 6, 85.0f, "Initializing OpenGL resources...");

    setStage(progress, 7, 95.0f, "Finalizing...");

    setStage(progress, 8, 100.0f, "Ready!");
    return level;
}
