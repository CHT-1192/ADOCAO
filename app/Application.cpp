#include "Application.h"
#include "LauncherWindow.h"
#include "LoadingWindow.h"
#include "LevelLoader.h"
#include "GameWindow.h"
#include "util/Logger.h"
#include <GLFW/glfw3.h>

#ifdef _WIN32
#include <windows.h>

// Pin current thread to a performance core (big.LITTLE aware).
// Uses GetSystemCpuSetInformation via dynamic load for MinGW compat.
typedef BOOL (WINAPI *PGSCSI)(PSYSTEM_CPU_SET_INFORMATION, ULONG, PULONG, HANDLE, ULONG);

static void pinToBigCore() {
    HMODULE k = GetModuleHandleA("kernel32.dll");
    if (!k) return;
    auto pfn = (PGSCSI)GetProcAddress(k, "GetSystemCpuSetInformation");
    if (!pfn) return;

    ULONG len = 0;
    pfn(nullptr, 0, &len, GetCurrentProcess(), 0);
    if (len == 0) return;

    auto* sets = (SYSTEM_CPU_SET_INFORMATION*)malloc(len);
    if (!sets) return;
    if (!pfn(sets, len, &len, GetCurrentProcess(), 0)) {
        free(sets); return;
    }

    DWORD_PTR mask = 0;
    for (ULONG off = 0; off * sizeof(*sets) < len; ) {
        auto& s = sets[off];
        if (s.Type == 0 && s.CpuSet.EfficiencyClass == 1)  // perf core
            mask |= (DWORD_PTR)1 << s.CpuSet.Id;
        off += s.Size;
    }
    free(sets);

    if (mask)
        SetThreadAffinityMask(GetCurrentThread(), mask);
}

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
    pinToBigCore();
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

int runApplicationFromCLI(const LauncherConfig& cfg, bool debugConsole) {
    std::string logPath = "ADOCAO.log";
    Logger::instance().init(logPath, debugConsole);

    LOG_I("ADOCAO starting (CLI mode)...");

#ifdef _WIN32
    enableDPIAwareness();
    pinToBigCore();
#endif

    if (!glfwInit()) {
        LOG_E("Failed to initialize GLFW");
        return 1;
    }

    LauncherConfig config = cfg;
    if (debugConsole) config.enableHitsounds = false;

    LOG_I("CLI: level=%s, music=%s, resolution=%dx%d, fullscreen=%d",
          config.levelPath.c_str(), config.musicPath.c_str(),
          config.resolutionW, config.resolutionH, config.fullscreen);

    LoadResult loadResult;
    showLoadingWindow([&](LoadingProgress& progress) {
        runLevelLoading(config, progress, loadResult);
    });

    if (!loadResult.level) {
        LOG_E("Failed to load level");
        glfwTerminate();
        return 1;
    }

    LOG_I("Level loaded: %zu tiles, BPM=%.1f", loadResult.level->tiles.size(), loadResult.level->settings.bpm);

    showGameWindow(config, loadResult);

    LOG_I("Game window closed, exiting.");
    glfwTerminate();
    return 0;
}
