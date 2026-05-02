#include "LauncherWindow.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "tinyfiledialogs.h"
#include <GLFW/glfw3.h>
#include <cstdio>
#include <array>
#include <string>

#ifdef _WIN32
#include <windows.h>

// tinyfiledialogs returns paths in system ANSI codepage. Convert to UTF-8.
static std::string ansiToUtf8(const char* ansi) {
    int wlen = MultiByteToWideChar(CP_ACP, 0, ansi, -1, nullptr, 0);
    if (wlen <= 0) return ansi;
    std::wstring wstr(wlen, L'\0');
    MultiByteToWideChar(CP_ACP, 0, ansi, -1, &wstr[0], wlen);

    int u8len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (u8len <= 0) return ansi;
    std::string u8str(u8len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &u8str[0], u8len, nullptr, nullptr);
    // Strip trailing null from string
    if (!u8str.empty() && u8str.back() == '\0') u8str.pop_back();
    return u8str;
}
#endif

static constexpr int LAUNCHER_W = 520;
static constexpr int LAUNCHER_H = 420;

LauncherConfig showLauncher() {
    // ---- Create window ----
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(LAUNCHER_W, LAUNCHER_H, "ADOCAO - Launcher", nullptr, nullptr);
    if (!window) {
        fprintf(stderr, "Failed to create launcher window\n");
        return {.cancelled = true};
    }

    // Center on primary monitor
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (monitor) {
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        if (mode) {
            glfwSetWindowPos(window,
                (mode->width  - LAUNCHER_W) / 2,
                (mode->height - LAUNCHER_H) / 2);
        }
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    // ---- Init ImGui ----
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    // Load CJK font (try multiple system fonts)
    {
        ImGuiIO& io = ImGui::GetIO();
        const char* cjkFonts[] = {
            "C:/Windows/Fonts/msyh.ttc",      // Microsoft YaHei (Chinese)
            "C:/Windows/Fonts/malgun.ttf",    // Malgun Gothic (Korean)
            "C:/Windows/Fonts/msgothic.ttc",  // MS Gothic (Japanese)
            "C:/Windows/Fonts/simhei.ttf",    // SimHei (Chinese)
            "C:/Windows/Fonts/gulim.ttc",     // Gulim (Korean)
        };

        // Build CJK glyph ranges
        static const ImWchar cjkRanges[] = {
            0x0020, 0x00FF,   // Basic Latin + Latin Supplement
            0x2000, 0x206F,   // General Punctuation
            0x3000, 0x303F,   // CJK Symbols
            0x3040, 0x309F,   // Hiragana
            0x30A0, 0x30FF,   // Katakana
            0x3130, 0x318F,   // Hangul Compatibility Jamo
            0x4E00, 0x9FFF,   // CJK Unified Ideographs
            0xAC00, 0xD7AF,   // Hangul Syllables
            0xFF00, 0xFFEF,   // Half-width / Full-width forms
            0,
        };

        bool loaded = false;
        for (const char* path : cjkFonts) {
            FILE* f = fopen(path, "rb");
            if (f) { fclose(f); io.Fonts->AddFontFromFileTTF(path, 16.0f, nullptr, cjkRanges); loaded = true; break; }
        }
        if (!loaded) {
            io.Fonts->AddFontDefault();
        }
    }

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    // ---- Config state ----
    LauncherConfig cfg;
    cfg.levelPath.reserve(512);
    cfg.musicPath.reserve(512);

    char levelBuf[1024] = {};
    char musicBuf[1024] = {};
    int  resoIdx = 0; // 0=1920x1080, 1=2560x1440, 2=3840x2160, 3=1280x720
    const std::array<const char*, 4> resoNames = {"1920x1080", "2560x1440", "3840x2160", "1280x720"};
    const std::array<int, 4> resoW = {1920, 2560, 3840, 1280};
    const std::array<int, 4> resoH = {1080, 1440, 2160, 720};

    bool done = false;

    // ---- Main loop ----
    while (!glfwWindowShouldClose(window) && !done) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Full-window panel
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)LAUNCHER_W, (float)LAUNCHER_H));
        ImGui::Begin("Launcher", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);

        ImGui::Spacing();
        ImGui::SetCursorPosX(60);
        ImGui::Text("ADOCAO - A Dance of Fire and Ice Level Player");
        ImGui::Separator();
        ImGui::Spacing();

        // Level file
        ImGui::Text("Level file (.adofai):");
        ImGui::SetNextItemWidth(380);
        ImGui::InputText("##level", levelBuf, sizeof(levelBuf));
        ImGui::SameLine();
        if (ImGui::Button("Browse##lvl")) {
            const char* filters[] = {"*.adofai", "*.json", "*.zip"};
            const char* path = tinyfd_openFileDialog(
                "Select level file", "", 3, filters, "ADOFAI files", 0);
            if (path) {
    #ifdef _WIN32
            snprintf(levelBuf, sizeof(levelBuf), "%s", ansiToUtf8(path).c_str());
#else
            snprintf(levelBuf, sizeof(levelBuf), "%s", path);
#endif
            }
        }

        ImGui::Spacing();

        // Music file
        ImGui::Text("Music file:");
        ImGui::SetNextItemWidth(380);
        ImGui::InputText("##music", musicBuf, sizeof(musicBuf));
        ImGui::SameLine();
        if (ImGui::Button("Browse##mus")) {
            const char* filters[] = {"*.ogg", "*.mp3", "*.wav", "*.flac", "*.aac"};
            const char* path = tinyfd_openFileDialog(
                "Select music file", "", 5, filters, "Audio files", 0);
            if (path) {
#ifdef _WIN32
                snprintf(musicBuf, sizeof(musicBuf), "%s", ansiToUtf8(path).c_str());
#else
                snprintf(musicBuf, sizeof(musicBuf), "%s", path);
#endif
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Resolution
        ImGui::Text("Resolution:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200);
        ImGui::Combo("##reso", &resoIdx, resoNames.data(), (int)resoNames.size());

        // Fullscreen
        ImGui::SameLine();
        ImGui::Spacing(); ImGui::SameLine();
        ImGui::Checkbox("Fullscreen", &cfg.fullscreen);

        ImGui::Spacing();
        ImGui::Spacing();

        // Start button (centered at bottom)
        ImGui::SetCursorPosY((float)LAUNCHER_H - 60);
        ImGui::SetCursorPosX((float)(LAUNCHER_W - 120) / 2.0f);
        if (ImGui::Button("Start", ImVec2(120, 36))) {
            cfg.levelPath    = levelBuf;
            cfg.musicPath    = musicBuf;
            cfg.resolutionW  = resoW[resoIdx];
            cfg.resolutionH  = resoH[resoIdx];
            cfg.cancelled    = false;
            done = true;
        }

        ImGui::End();

        // Render
        ImGui::Render();
        int displayW, displayH;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    if (!done) cfg.cancelled = true;

    // ---- Cleanup ----
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);

    return cfg;
}
