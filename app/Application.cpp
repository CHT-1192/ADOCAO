#include "Application.h"
#include "LauncherWindow.h"
#include "LoadingWindow.h"
#include "LevelLoader.h"
#include "GameWindow.h"
#include "util/Logger.h"
#include <GLFW/glfw3.h>

#ifdef _WIN32
#include <windows.h>

static void enableDPIAwareness() {
    HMODULE shcore = LoadLibraryA("shcore.dll");
    if (shcore) {
        auto SetProcessDpiAwareness = (HRESULT(WINAPI*)(int))
            GetProcAddress(shcore, "SetProcessDpiAwareness");
        if (SetProcessDpiAwareness) SetProcessDpiAwareness(2); // PerMonitor
        FreeLibrary(shcore);
    } else {
        HMODULE user32 = LoadLibraryA("user32.dll");
        if (user32) {
            auto SetProcessDPIAware = (BOOL(WINAPI*)())
                GetProcAddress(user32, "SetProcessDPIAware");
            if (SetProcessDPIAware) SetProcessDPIAware();
            FreeLibrary(user32);
        }
    }
}
#endif

int runApplication(bool debugConsole) {
    // Determine log path next to executable
    std::string logPath = "ADOCAO.log";
    Logger::instance().init(logPath, debugConsole);

    LOG_I("ADOCAO starting...");

#ifdef _WIN32
    enableDPIAwareness();
#endif

    if (!glfwInit()) {
        LOG_E("Failed to initialize GLFW");
        return 1;
    }

    // Stage 1: Launcher
    LauncherConfig cfg = showLauncher();
    if (debugConsole) cfg.enableHitsounds = false;
    if (cfg.cancelled || cfg.levelPath.empty()) {
        LOG_I("Launcher cancelled, exiting.");
        glfwTerminate();
        return 0;
    }
    LOG_I("Launcher: level=%s, music=%s, resolution=%dx%d, fullscreen=%d",
          cfg.levelPath.c_str(), cfg.musicPath.c_str(),
          cfg.resolutionW, cfg.resolutionH, cfg.fullscreen);

    // Stage 2: Loading
    LoadResult loadResult;
    showLoadingWindow([&](LoadingProgress& progress) {
        runLevelLoading(cfg, progress, loadResult);
    });

    if (!loadResult.level) {
        LOG_E("Failed to load level");
        glfwTerminate();
        return 1;
    }

    LOG_I("Level loaded: %zu tiles, BPM=%.1f", loadResult.level->tiles.size(), loadResult.level->settings.bpm);

    // Stage 3: Game
    showGameWindow(cfg, loadResult);

    LOG_I("Game window closed, exiting.");
    glfwTerminate();
    return 0;
}
