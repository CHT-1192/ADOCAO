#include "LauncherWindow.hpp"
#include "LevelLoader.hpp"
#include "LoadingWindow.hpp"
#include "glad/gl_core.hpp"
#include "core/util/Logger.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "tinyfiledialogs.h"
#include <GLFW/glfw3.h>
#include <cstdio>
#include <array>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <filesystem>

#ifdef _WIN32
#include <shobjidl.h>

static std::string win32OpenFileDialog(const wchar_t* title, const wchar_t* filters) {
    bool comInitialized = (CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED) == S_OK);

    auto cleanup = [&](std::string r) {
        if (comInitialized) CoUninitialize();
        return r;
    };

    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                                 IID_IFileOpenDialog, (void**)&dlg)))
        return cleanup({});

    // Parse filter string "*.adofai\0*.json\0*.zip\0\0"
    std::vector<COMDLG_FILTERSPEC> specs;
    std::vector<std::wstring> specStrs;
    {
        std::wstring filterStr(filters);
        size_t start = 0;
        while (start < filterStr.length()) {
            size_t end = filterStr.find(L'\0', start);
            if (end == std::wstring::npos) break;
            std::wstring pat = filterStr.substr(start, end - start);
            std::wstring desc = pat + L" files";
            specStrs.push_back(desc);
            specStrs.push_back(pat);
            specs.push_back({specStrs[specStrs.size()-2].c_str(),
                             specStrs[specStrs.size()-1].c_str()});
            start = end + 1;
            if (filterStr[start] == L'\0') break;
        }
    }
    if (!specs.empty())
        dlg->SetFileTypes((UINT)specs.size(), specs.data());

    dlg->SetTitle(title);

    if (FAILED(dlg->Show(nullptr))) { dlg->Release(); return cleanup({}); }

    IShellItem* item = nullptr;
    if (FAILED(dlg->GetResult(&item))) { dlg->Release(); return cleanup({}); }

    wchar_t* rawPath = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath))) {
        item->Release(); dlg->Release(); return cleanup({});
    }

    // Wide → UTF-8
    int u8len = WideCharToMultiByte(CP_UTF8, 0, rawPath, -1, nullptr, 0, nullptr, nullptr);
    std::string result(u8len ? u8len - 1 : 0, '\0');
    if (u8len > 1)
        WideCharToMultiByte(CP_UTF8, 0, rawPath, -1, &result[0], u8len, nullptr, nullptr);

    CoTaskMemFree(rawPath);
    item->Release();
    dlg->Release();
    return cleanup(result);
}

static std::string openFileDialog(const char* title, const char* filterStr) {
    std::wstring wtitle(title, title + strlen(title));
    std::wstring wfilter(filterStr, filterStr + strlen(filterStr));
    return win32OpenFileDialog(wtitle.c_str(), wfilter.c_str());
}

static std::string win32SelectFolderDialog(const wchar_t* title, const std::string& initialDir) {
    bool comInitialized = (CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED) == S_OK);
    auto cleanup = [&](std::string r) {
        if (comInitialized) CoUninitialize();
        return r;
    };
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                                 IID_IFileOpenDialog, (void**)&dlg)))
        return cleanup({});
    DWORD opts;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS);
    dlg->SetTitle(title);
    if (!initialDir.empty()) {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, initialDir.c_str(), -1, nullptr, 0);
        std::wstring wdir(wlen ? wlen - 1 : 0, L'\0');
        if (wlen > 1)
            MultiByteToWideChar(CP_UTF8, 0, initialDir.c_str(), -1, &wdir[0], wlen);
        IShellItem* folder = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(wdir.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
            dlg->SetFolder(folder);
            folder->Release();
        }
    }
    if (FAILED(dlg->Show(nullptr))) { dlg->Release(); return cleanup({}); }
    IShellItem* item = nullptr;
    if (FAILED(dlg->GetResult(&item))) { dlg->Release(); return cleanup({}); }
    wchar_t* rawPath = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath))) {
        item->Release(); dlg->Release(); return cleanup({});
    }
    int u8len = WideCharToMultiByte(CP_UTF8, 0, rawPath, -1, nullptr, 0, nullptr, nullptr);
    std::string result(u8len ? u8len - 1 : 0, '\0');
    if (u8len > 1)
        WideCharToMultiByte(CP_UTF8, 0, rawPath, -1, &result[0], u8len, nullptr, nullptr);
    CoTaskMemFree(rawPath);
    item->Release();
    dlg->Release();
    return cleanup(result);
}

static std::string selectFolderDialog(const char* title, const std::string& initialDir) {
    std::wstring wtitle(title, title + strlen(title));
    return win32SelectFolderDialog(wtitle.c_str(), initialDir);
}
#else
static std::string openFileDialog(const char* title, const char* filterStr) {
    const char* path = tinyfd_openFileDialog(title, "", 0, nullptr, filterStr, 0);
    return path ? std::string(path) : std::string();
}

static std::string selectFolderDialog(const char* title, const std::string& initialDir) {
    const char* path = tinyfd_selectFolderDialog(title, initialDir.c_str());
    return path ? std::string(path) : std::string();
}
#endif

static bool isValidHexColor(const char* buf) {
    int len = 0;
    while (buf[len]) len++;
    if (len != 6) return false;
    for (int i = 0; i < 6; i++) {
        char c = buf[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')))
            return false;
    }
    return true;
}

// Auto-detect the music file: same-name audio next to the .adofai
// (.ogg/.mp3/.wav/.flac/.m4a). Returns "" if nothing found.
static std::string detectMusicFile(const std::string& levelPath) {
    namespace fs = std::filesystem;
    fs::path lvl(levelPath);
    fs::path dir = lvl.parent_path();
    if (dir.empty()) dir = ".";
    std::string stem = lvl.stem().string();
    for (const char* ext : {".ogg",".OGG",".mp3",".MP3",".wav",".WAV",".flac",".FLAC",".m4a",".M4A"}) {
        fs::path cand = dir / (stem + ext);
        if (fs::exists(cand)) return cand.string();
    }
    return "";
}

static constexpr int LAUNCHER_W = 520;
static constexpr int LAUNCHER_H = 420;

enum class Page : int { Welcome = 0, Preload, Hitsounds, Graphics, Visuals, Music };

LauncherConfig showLauncher() {
    // Window size in logical points. The GLFW + ImGui backends handle high-DPI
    // (Retina) scaling automatically via DisplayFramebufferScale.
    int winW = LAUNCHER_W;
    int winH = LAUNCHER_H;

    // ---- Create window ----
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(winW, winH, "ADOCAO", nullptr, nullptr);
    if (!window) {
        fprintf(stderr, "Failed to create launcher window\n");
        return {.cancelled = true};
    }

    // Center on primary monitor (work area in logical points)
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (monitor) {
        int wx, wy, ww, wh;
        glfwGetMonitorWorkarea(monitor, &wx, &wy, &ww, &wh);
        glfwSetWindowPos(window, wx + (ww - winW) / 2, wy + (wh - winH) / 2);
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    // Load GL function pointers (glad). The wizard renders via ImGui/GL right
    // away, before GameWindow::init would normally call loadGLCore().
    if (!loadGLCore()) {
        fprintf(stderr, "Failed to load OpenGL functions\n");
        glfwDestroyWindow(window);
        return {.cancelled = true};
    }

    // ---- Init ImGui ----
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    {   // Rounded corners (radius 4)
        ImGuiStyle& st = ImGui::GetStyle();
        st.WindowRounding    = 4.0f;
        st.FrameRounding     = 4.0f;
        st.PopupRounding     = 4.0f;
        st.ChildRounding     = 4.0f;
        st.ScrollbarRounding = 4.0f;
        st.GrabRounding      = 4.0f;
        st.TabRounding       = 4.0f;
    }

    // High-DPI fonts: rasterize glyphs at physical resolution (Retina = 2x),
    // then scale layout back to logical size via FontGlobalScale.
    float dpiScale = 1.0f;
    {
        GLFWmonitor* mon = glfwGetPrimaryMonitor();
        if (mon) {
            float sx, sy;
            glfwGetMonitorContentScale(mon, &sx, &sy);
            dpiScale = std::max(sx, sy);
        }
    }
    float fontSize = 12.0f * dpiScale;

    // Fonts: main (with CJK merge) + a larger title font (Latin only)
    ImFont* mainFont = nullptr;
    ImFont* titleFont = nullptr;
    {
        ImGuiIO& io = ImGui::GetIO();
        const char* cjkFonts[] = {
#ifdef _WIN32
            "C:/Windows/Fonts/malgun.ttf",
            "C:/Windows/Fonts/NanumGothic.ttf",
            "C:/Windows/Fonts/gulim.ttc",
            "C:/Windows/Fonts/batang.ttc",
            "C:/Windows/Fonts/msyh.ttc",
            "C:/Windows/Fonts/msgothic.ttc",
            "C:/Windows/Fonts/simhei.ttf",
#elif defined(__APPLE__)
            "/System/Library/Fonts/PingFang.ttc",
            "/System/Library/Fonts/STHeiti Light.ttc",
            "/System/Library/Fonts/Hiragino Sans GB.ttc",
            "/System/Library/Fonts/Supplemental/Songti.ttc",
#else
            "/usr/share/fonts/noto/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/wqy-microhei/wqy-microhei.ttc",
            "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
            "/usr/share/fonts/nanum/NanumGothic.ttf",
            "/usr/share/fonts/truetype/nanum/NanumGothic.ttf",
#endif
        };
        static const ImWchar latinRange[] = { 0x0020, 0x00FF, 0 };
        static const ImWchar cjkRanges[] = {
            0x2000, 0x206F, 0x3000, 0x303F, 0x3040, 0x309F,
            0x30A0, 0x30FF, 0x3130, 0x318F, 0x4E00, 0x9FFF,
            0xAC00, 0xD7AF, 0xFF00, 0xFFEF, 0,
        };

        // Main Latin font
        {
            const char* latinFonts[] = {
#ifdef _WIN32
                "C:/Windows/Fonts/segoeui.ttf",
                "C:/Windows/Fonts/arial.ttf",
#elif defined(__APPLE__)
                "/System/Library/Fonts/Helvetica.ttc",
                "/System/Library/Fonts/SFNS.ttf",
#endif
            };
            bool latinLoaded = false;
            for (const char* path : latinFonts) {
                FILE* f = fopen(path, "rb");
                if (f) { fclose(f); mainFont = io.Fonts->AddFontFromFileTTF(path, fontSize); latinLoaded = true; break; }
            }
            if (!latinLoaded) mainFont = io.Fonts->AddFontDefault();
        }

        // Title font (bigger, Latin only)
        {
            const char* latinFonts[] = {
#ifdef _WIN32
                "C:/Windows/Fonts/segoeui.ttf",
                "C:/Windows/Fonts/arial.ttf",
#elif defined(__APPLE__)
                "/System/Library/Fonts/Helvetica.ttc",
                "/System/Library/Fonts/SFNS.ttf",
#endif
            };
            for (const char* path : latinFonts) {
                FILE* f = fopen(path, "rb");
                if (f) { fclose(f); titleFont = io.Fonts->AddFontFromFileTTF(path, fontSize * 1.8f); break; }
            }
            if (!titleFont) titleFont = mainFont;
        }

        // Merge CJK into the main font
        ImFontConfig cfg;
        cfg.MergeMode = true;
        for (const char* path : cjkFonts) {
            FILE* f = fopen(path, "rb");
            if (f) { fclose(f); io.Fonts->AddFontFromFileTTF(path, fontSize, &cfg, cjkRanges); break; }
        }
    }

    // Scale layout back to logical size; glyphs stay crisp at physical res.
    ImGui::GetIO().FontGlobalScale = 1.0f / dpiScale;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    // ---- Config state ----
    LauncherConfig cfg;
    cfg.levelPath.reserve(512);
    cfg.musicPath.reserve(512);

    char levelBuf[1024] = {};
    char musicBuf[1024] = {};
    char fillBuf[8]   = "DEBB7B";
    char strokeBuf[8] = "6F5D3D";
    char bgBuf[8]     = "000000";
    bool autoStroke = true;
    bool enableHitsounds = true;
    bool forceHS = false;
    int  forceHSIdx = 0;          // 0 = Kick
    bool preloadEnabled = true;   // Preload checkbox (welcome page)
    bool showTrail = true;
    float trailDuration = 0.4f;
    float trailSampleRate = 200.0f;
    bool fullscreen = false;
    bool legacyCulling = false;
    int  msaaIdx = 0;             // 0 = Off
    const std::array<const char*, 4> msaaNames = {"Off", "2x", "4x", "8x"};
    const std::array<int, 4> msaaSamplesArr = {0, 2, 4, 8};
    int  resoIdx = 2;             // default: 1920x1080
    const std::array<const char*, 5> resoNames = {"960x540", "1280x720", "1920x1080", "2560x1440", "3840x2160"};
    const std::array<int, 5> resoW = {960, 1280, 1920, 2560, 3840};
    const std::array<int, 5> resoH = {540, 720, 1080, 1440, 2160};
    const std::array<const char*, 28> hsTypes = {
        "Kick","KickHouse","KickChroma","KickRupture",
        "Snare","SnareHouse","SnareVapor","Clap","ClapHit","ClapHitEcho",
        "Hat","HatHouse","Chuck","Hammer","Shaker","ShakerLoud",
        "Sidestick","Stick","ReverbClack","ReverbClap","Squareshot",
        "FireTile","IceTile","PowerUp","PowerDown","VehiclePositive",
        "VehicleNegative","Sizzle"
    };

    std::string lastError;
    bool musicAutoTried = false;
    bool done = false;
    bool windowCloseRequested = false;

    // Wizard state
    Page page = Page::Welcome;
    LoadingProgress preloadProgress;
    std::thread preloadThread;
    std::atomic<bool> preloadFinished{false};
    bool preloadRunning = false;

    // ---- Main loop ----
    while (!glfwWindowShouldClose(window) && !done) {
        glfwPollEvents();

        // Preload finished → advance to the Hitsounds page
        if (preloadRunning && preloadFinished.load()) {
            if (preloadThread.joinable()) preloadThread.join();
            preloadRunning = false;
            if (cfg.preloadedLevel && cfg.preloadedTimeline) {
                page = Page::Hitsounds;
                // Auto-fill music from the parsed level (settings.musicFile)
                if (musicBuf[0] == '\0') {
                    std::string autoMusic = detectMusicFile(cfg.levelPath);
                    if (!autoMusic.empty())
                        snprintf(musicBuf, sizeof(musicBuf), "%s", autoMusic.c_str());
                }
            } else {
                lastError = "Failed to load level. Please check the file.";
                page = Page::Welcome;
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Full-window panel
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)winW, (float)winH));
        ImGui::Begin("Launcher", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);

        float S = 1.0f;  // hardcoded sizes are in logical pixels
        const float padX = 24.0f * S;
        const float contentW = (float)winW - 2.0f * padX;
        const float btnW = 110.0f * S;
        const float btnH = 30.0f * S;

        switch (page) {

        // ============ WELCOME ============
        case Page::Welcome: {
            // Big centered title
            float titleY = 70.0f * S;
            ImGui::SetCursorPosY(titleY);
            const char* title = "Welcome to ADOCAO";
            if (titleFont) ImGui::PushFont(titleFont);
            ImVec2 ts = ImGui::CalcTextSize(title);
            ImGui::SetCursorPosX((float)(winW - (int)ts.x) / 2.0f);
            ImGui::Text("%s", title);
            if (titleFont) ImGui::PopFont();
            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Level file picker (path + Choose), centered as a group
            float fileW = 300.0f * S;
            float chooseW = 62.0f * S;
            float gap = 8.0f * S;
            float groupW = fileW + gap + chooseW;
            float x0 = ((float)winW - groupW) / 2.0f;

            ImGui::SetCursorPosY(titleY + 46.0f * S);
            ImGui::SetCursorPosX(x0);
            ImGui::SetNextItemWidth(fileW);
            ImGui::InputText("##level", levelBuf, sizeof(levelBuf));
            ImGui::SameLine(0.0f, gap);
            if (ImGui::Button("Choose", ImVec2(chooseW, 0))) {
                auto result = openFileDialog("Select level file", "*.adofai");
                if (!result.empty()) {
                    snprintf(levelBuf, sizeof(levelBuf), "%s", result.c_str());
                    lastError.clear();
                }
            }

            // Preload checkbox, left edge aligned with the file box
            ImGui::SetCursorPosX(x0);
            ImGui::Checkbox("Preload", &preloadEnabled);
            ImGui::Spacing();
            if (!lastError.empty()) {
                ImGui::SetCursorPosX(x0);
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", lastError.c_str());
            }

            // Next / Export buttons centered
            ImGui::SetCursorPosY((float)winH - 56.0f * S);
            float xb = ((float)winW - (btnW * 2.0f + 12.0f * S)) / 2.0f;
            ImGui::SetCursorPosX(xb);
            bool levelOk = levelBuf[0] != '\0';
            if (!levelOk) ImGui::BeginDisabled();
            if (ImGui::Button("Next", ImVec2(btnW, btnH))) {
                if (levelOk) {
                    cfg.levelPath = levelBuf;
                    lastError.clear();
                    if (preloadEnabled) {
                        // Start background preload (parse + timeline)
                        preloadProgress.percent.store(0.0f);
                        preloadProgress.stage.store(0);
                        {
                            std::lock_guard<std::mutex> lk(preloadProgress.textMutex);
                            snprintf(preloadProgress.stageText, sizeof(preloadProgress.stageText),
                                     "Starting...");
                        }
                        preloadFinished.store(false);
                        preloadRunning = true;
                        cfg.preloadedLevel.reset();
                        cfg.preloadedTimeline.reset();
                        preloadThread = std::thread([&]() {
                            runLevelPreload(cfg, preloadProgress,
                                            cfg.preloadedLevel, cfg.preloadedTimeline);
                            preloadFinished.store(true);
                        });
                        page = Page::Preload;
                    } else {
                        page = Page::Hitsounds;
                    }
                }
            }
            if (!levelOk) ImGui::EndDisabled();
            ImGui::SameLine(0.0f, 12.0f * S);
            if (ImGui::Button("Export", ImVec2(btnW, btnH))) {
                if (levelOk) {
                    // Choose export directory, default = level's folder
                    std::string defDir;
                    {
                        std::string lvl(levelBuf);
                        auto slash = lvl.find_last_of("/\\");
                        defDir = slash == std::string::npos ? "." : lvl.substr(0, slash);
                    }
                    std::string dir = selectFolderDialog("Select export folder", defDir);
                    if (!dir.empty()) {
                        cfg.levelPath = levelBuf;
                        cfg.exportDir = dir;
                        cfg.enableHitsounds = true;
                        cfg.exportHitsounds = true;
                        cfg.cancelled = false;
                        done = true;
                    }
                }
            }
            break;
        }

        // ============ PRELOAD (transition) ============
        case Page::Preload: {
            ImGui::Spacing();
            ImGui::Spacing();
            float barW = contentW;
            ImGui::SetCursorPosX(padX);
            if (mainFont) ImGui::PushFont(mainFont);
            ImVec2 t0 = ImGui::CalcTextSize("Preloading level...");
            ImGui::SetCursorPosX(((float)winW - t0.x) / 2.0f);
            ImGui::Text("Preloading level...");
            ImGui::Spacing();
            ImGui::SetCursorPosX(padX);
            float pct = preloadProgress.percent.load();
            ImGui::ProgressBar(pct / 100.0f, ImVec2(barW, 22.0f * S));
            ImGui::Spacing();
            {
                std::lock_guard<std::mutex> lock(preloadProgress.textMutex);
                ImVec2 ts2 = ImGui::CalcTextSize(preloadProgress.stageText);
                ImGui::SetCursorPosX(((float)winW - ts2.x) / 2.0f);
                ImGui::Text("%s", preloadProgress.stageText);
            }
            if (mainFont) ImGui::PopFont();
            break;
        }

        // ============ GENERIC CONFIG PAGE HEADER ============
        default: {
            // Falls through to per-page content below via helper lambdas defined after.
        }
        }

        if (page == Page::Hitsounds || page == Page::Graphics ||
            page == Page::Visuals || page == Page::Music) {
            // Progress: welcome = 0%, Start page (Music) = 100%, linear in between.
            float progress = 0.0f;
            const char* title = "";
            switch (page) {
                case Page::Hitsounds: progress = 25.0f; title = "Hitsounds"; break;
                case Page::Graphics:  progress = 50.0f; title = "Graphics";  break;
                case Page::Visuals:   progress = 75.0f; title = "Visuals";   break;
                case Page::Music:     progress = 100.0f; title = "Music";    break;
                default: break;
            }
            ImGui::SetCursorPosY(18.0f * S);
            ImGui::SetCursorPosX(padX);
            ImGui::ProgressBar(progress / 100.0f, ImVec2(contentW, 10.0f * S));
            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::SetCursorPosX(padX);
            ImGui::Text("%s", title);
            ImGui::Spacing();
            ImGui::SetCursorPosX(padX);
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::Spacing();

            const float colX = padX;
            const float ctrlX = padX + 150.0f * S;

            if (page == Page::Hitsounds) {
                ImGui::SetCursorPosX(colX);
                ImGui::Checkbox("Enable hitsounds", &enableHitsounds);
                if (!enableHitsounds) ImGui::BeginDisabled();
                ImGui::Spacing();
                ImGui::Indent(24.0f * S);
                ImGui::Checkbox("Override hit sound type", &forceHS);
                if (forceHS) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(150.0f * S);
                    ImGui::Combo("##forceHSType", &forceHSIdx, hsTypes.data(), (int)hsTypes.size());
                }
                ImGui::Unindent(24.0f * S);
                if (!enableHitsounds) ImGui::EndDisabled();
            }
            else if (page == Page::Graphics) {
                // Resolution
                ImGui::SetCursorPosX(colX); ImGui::Text("Resolution");
                ImGui::SameLine(ctrlX);
                ImGui::SetNextItemWidth(contentW - (ctrlX - padX) - 24.0f * S);
                ImGui::Combo("##reso", &resoIdx, resoNames.data(), (int)resoNames.size());
                ImGui::Spacing();
                ImGui::SetCursorPosX(colX); ImGui::Checkbox("Fullscreen", &fullscreen);
                ImGui::Spacing();
                // MSAA (Off/2x/4x/8x)
                ImGui::SetCursorPosX(colX); ImGui::Text("MSAA");
                ImGui::SameLine(ctrlX);
                ImGui::SetNextItemWidth(contentW - (ctrlX - padX) - 24.0f * S);
                ImGui::Combo("##msaa", &msaaIdx, msaaNames.data(), (int)msaaNames.size());
                ImGui::Spacing();
                ImGui::SetCursorPosX(colX); ImGui::Checkbox("Legacy Culling", &legacyCulling);
            }
            else if (page == Page::Visuals) {
                // Trail
                ImGui::SetCursorPosX(colX);
                ImGui::Checkbox("Trail", &showTrail);
                if (!showTrail) ImGui::BeginDisabled();
                ImGui::Spacing();
                ImGui::Indent(24.0f * S);
                ImGui::SetCursorPosX(colX + 24.0f * S); ImGui::Text("Sample rate");
                ImGui::SameLine(ctrlX + 0.0f);
                ImGui::SetNextItemWidth(140.0f * S);
                ImGui::DragFloat("##srate", &trailSampleRate, 2.0f, 5.0f, 500.0f, "%.0f/s");
                ImGui::Unindent(24.0f * S);
                ImGui::Spacing();
                ImGui::SetCursorPosX(colX); ImGui::Text("Duration");
                ImGui::SameLine(ctrlX);
                ImGui::SetNextItemWidth(140.0f * S);
                ImGui::DragFloat("##dur", &trailDuration, 0.005f, 0.05f, 2.0f, "%.2fs");
                if (!showTrail) ImGui::EndDisabled();
                ImGui::Spacing();

                // Fill / stroke / background colors
                bool fillOk   = isValidHexColor(fillBuf);
                bool strokeOk = isValidHexColor(strokeBuf);
                bool bgOk     = isValidHexColor(bgBuf);
                ImGui::SetCursorPosX(colX); ImGui::Text("Fill #");
                ImGui::SameLine(ctrlX);
                ImGui::SetNextItemWidth(90.0f * S);
                ImGui::InputText("##fill", fillBuf, 7, ImGuiInputTextFlags_CharsUppercase);
                if (!fillOk && fillBuf[0]) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1,0.3f,0.3f,1),"(6 hex)"); }
                ImGui::Spacing();
                ImGui::SetCursorPosX(colX); ImGui::Checkbox("Auto stroke", &autoStroke);
                ImGui::Spacing();
                ImGui::Indent(24.0f * S);
                if (autoStroke) ImGui::BeginDisabled();
                ImGui::SetCursorPosX(colX + 24.0f * S); ImGui::Text("Stroke #");
                ImGui::SameLine(ctrlX);
                ImGui::SetNextItemWidth(90.0f * S);
                ImGui::InputText("##stroke", strokeBuf, 7, ImGuiInputTextFlags_CharsUppercase);
                if (autoStroke) ImGui::EndDisabled();
                if (!strokeOk && strokeBuf[0]) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1,0.3f,0.3f,1),"(6 hex)"); }
                ImGui::Unindent(24.0f * S);
                ImGui::Spacing();
                ImGui::SetCursorPosX(colX); ImGui::Text("Background #");
                ImGui::SameLine(ctrlX);
                ImGui::SetNextItemWidth(90.0f * S);
                ImGui::InputText("##bg", bgBuf, 7, ImGuiInputTextFlags_CharsUppercase);
                if (!bgOk && bgBuf[0]) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1,0.3f,0.3f,1),"(6 hex)"); }

                // Auto-calculate stroke from fill when enabled
                if (autoStroke && fillOk) {
                    unsigned r,g,b; sscanf(fillBuf,"%02x%02x%02x",&r,&g,&b);
                    r=(unsigned)(r*0.5f); g=(unsigned)(g*0.5f); b=(unsigned)(b*0.5f);
                    snprintf(strokeBuf,sizeof(strokeBuf),"%02x%02x%02x",r,g,b);
                    strokeOk = true;
                }
            }
            else if (page == Page::Music) {
                // Auto-detect music once (from parsed level + same-dir files)
                if (!musicAutoTried) {
                    musicAutoTried = true;
                    if (musicBuf[0] == '\0' && cfg.preloadedLevel) {
                        std::string autoMusic = detectMusicFile(cfg.levelPath);
                        if (!autoMusic.empty())
                            snprintf(musicBuf, sizeof(musicBuf), "%s", autoMusic.c_str());
                    }
                }
                float fileW = 300.0f * S;
                float chooseW = 62.0f * S;
                ImGui::SetCursorPosX(colX);
                ImGui::Text("Music file");
                ImGui::Spacing();
                ImGui::SetCursorPosX(colX);
                ImGui::SetNextItemWidth(fileW);
                ImGui::InputText("##music", musicBuf, sizeof(musicBuf));
                ImGui::SameLine();
                if (ImGui::Button("Choose##mus", ImVec2(chooseW, 0))) {
                    auto result = openFileDialog("Select music file", "*.ogg");
                    if (!result.empty())
                        snprintf(musicBuf, sizeof(musicBuf), "%s", result.c_str());
                }
            }

            // Bottom navigation: Next on config pages, Start on the last page
            bool validHex = isValidHexColor(fillBuf) && isValidHexColor(strokeBuf) &&
                            isValidHexColor(bgBuf);
            bool canContinue = true;
            if (page == Page::Music) {
                canContinue = true;  // Start always available
            } else if (page == Page::Visuals) {
                canContinue = validHex;
            }
            ImGui::SetCursorPosY((float)winH - 56.0f * S);
            bool isStartPage = (page == Page::Music);
            float xb = ((float)winW - btnW) / 2.0f;
            ImGui::SetCursorPosX(xb);
            if (!canContinue) ImGui::BeginDisabled();
            const char* btnLabel = isStartPage ? "Start" : "Next";
            if (ImGui::Button(btnLabel, ImVec2(btnW, btnH))) {
                if (isStartPage) {
                    // Collect everything into cfg and leave the wizard
                    cfg.musicPath        = musicBuf;
                    cfg.trackFillColor   = fillBuf;
                    cfg.trackStrokeColor = strokeBuf;
                    cfg.backgroundColor  = bgBuf;
                    cfg.autoStroke       = autoStroke;
                    cfg.enableHitsounds  = enableHitsounds;
                    cfg.forceHitsoundType = forceHS ? hsTypes[forceHSIdx] : "";
                    cfg.legacyCulling    = legacyCulling;
                    cfg.msaaSamples      = msaaSamplesArr[msaaIdx];
                    cfg.exclusiveFullscreen = true;
                    cfg.resolutionW      = resoW[resoIdx];
                    cfg.resolutionH      = resoH[resoIdx];
                    cfg.fullscreen       = fullscreen;
                    cfg.showTrail        = showTrail;
                    cfg.trailDuration    = trailDuration;
                    cfg.trailSampleRate  = trailSampleRate;
                    cfg.cancelled        = false;
                    done = true;
                } else {
                    page = (Page)((int)page + 1);
                }
            }
            if (!canContinue) ImGui::EndDisabled();
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

    // Ensure preload thread is joined before tearing down
    if (preloadRunning && preloadThread.joinable()) {
        preloadThread.join();
        preloadRunning = false;
    }

    if (!done) cfg.cancelled = true;

    // ---- Cleanup ----
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);

    return cfg;
}
