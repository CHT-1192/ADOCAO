#include "LauncherWindow.hpp"
#include "util/Logger.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "tinyfiledialogs.h"
#include <GLFW/glfw3.h>
#include <cstdio>
#include <array>
#include <string>
#include <codecvt>
#include <locale>

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
#else
static std::string openFileDialog(const char* title, const char* filterStr) {
    const char* path = tinyfd_openFileDialog(title, "", 0, nullptr, filterStr, 0);
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

static constexpr int LAUNCHER_W = 520;
static constexpr int LAUNCHER_H = 420;

LauncherConfig showLauncher() {
    // ---- DPI scale ----
    float dpiScale = 1.0f;
    {
        GLFWmonitor* mon = glfwGetPrimaryMonitor();
        if (mon) {
            float sx, sy;
            glfwGetMonitorContentScale(mon, &sx, &sy);
            dpiScale = std::max(sx, sy);
        }
    }
    int winW = (int)(LAUNCHER_W * dpiScale);
    int winH = (int)(LAUNCHER_H * dpiScale);

    // ---- Create window ----
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(winW, winH, "ADOCAO - Launcher", nullptr, nullptr);
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
                (mode->width  - winW) / 2,
                (mode->height - winH) / 2);
        }
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    // ---- Init ImGui ----
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();

    // DPI-aware sizing: scale all UI elements, load fonts at native resolution
    ImGui::GetStyle().ScaleAllSizes(dpiScale);
    float fontSize = 16.0f * dpiScale;

    // Load CJK font (try multiple system fonts)
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

        // Glyph ranges: separate Latin from CJK so backslash stays as '\' not '₩'
        static const ImWchar latinRange[] = { 0x0020, 0x00FF, 0 };
        static const ImWchar cjkRanges[] = {
            0x2000, 0x206F, 0x3000, 0x303F, 0x3040, 0x309F,
            0x30A0, 0x30FF, 0x3130, 0x318F, 0x4E00, 0x9FFF,
            0xAC00, 0xD7AF, 0xFF00, 0xFFEF, 0,
        };

        // Load default font at DPI-aware size (replaces tiny 13px built-in)
        {
            const char* latinFonts[] = {
#ifdef _WIN32
                "C:/Windows/Fonts/segoeui.ttf",
                "C:/Windows/Fonts/arial.ttf",
#endif
            };
            bool latinLoaded = false;
            for (const char* path : latinFonts) {
                FILE* f = fopen(path, "rb");
                if (f) { fclose(f); io.Fonts->AddFontFromFileTTF(path, fontSize); latinLoaded = true; break; }
            }
            if (!latinLoaded) io.Fonts->AddFontDefault();
        }

        // Merge CJK font at DPI-aware size (only for non-Latin ranges)
        ImFontConfig cfg;
        cfg.MergeMode = true;
        bool cjkLoaded = false;
        for (const char* path : cjkFonts) {
            FILE* f = fopen(path, "rb");
            if (f) { fclose(f); io.Fonts->AddFontFromFileTTF(path, fontSize, &cfg, cjkRanges); cjkLoaded = true; break; }
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
    char fillBuf[8]   = "DEBB7B";
    char strokeBuf[8] = "6F5D3D";
    char bgBuf[8]     = "000000";
    bool autoStroke = true;
    bool enableHitsounds = true;
    bool forceHS = false;
    int forceHSIdx = 0;  // 0 = Kick
    bool exclusiveFS = true;
    bool legacyCulling = false;
    bool msaa = false;
#ifdef ADOCAO_EXTREME_ZOOM
    msaa = true;
#endif
    int msaaSIdx = 0;  // 0=2x, 1=4x, 2=8x
    const std::array<const char*, 3> msaaNames = {"2x", "4x", "8x (not recommended)"};
    const std::array<int, 3> msaaSamplesArr = {2, 4, 8};
    int  resoIdx = 2;  // default: 1920x1080
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

    bool done = false;

    // ---- Main loop ----
    while (!glfwWindowShouldClose(window) && !done) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Full-window panel
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)winW, (float)winH));
        ImGui::Begin("Launcher", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);

        float S = dpiScale;  // scale factor for all hardcoded sizes

        ImGui::Spacing();
        ImGui::SetCursorPosX(60 * S);
        ImGui::Text("ADOCAO - A Dance of C++ and OpenGL");
        ImGui::Separator();
        ImGui::Spacing();

        // Level file
        ImGui::Text("Level file (.adofai):");
        ImGui::SetNextItemWidth(380 * S);
        ImGui::InputText("##level", levelBuf, sizeof(levelBuf));
        ImGui::SameLine();
        if (ImGui::Button("Browse##lvl")) {
            auto result = openFileDialog("Select level file", "*.adofai");
            if (!result.empty()) {
                snprintf(levelBuf, sizeof(levelBuf), "%s", result.c_str());
            }
        }

        ImGui::Spacing();

        // Music file
        ImGui::Text("Music file:");
        ImGui::SetNextItemWidth(380 * S);
        ImGui::InputText("##music", musicBuf, sizeof(musicBuf));
        ImGui::SameLine();
        if (ImGui::Button("Browse##mus")) {
            auto result = openFileDialog("Select music file", "*.ogg");
            if (!result.empty())
                snprintf(musicBuf, sizeof(musicBuf), "%s", result.c_str());
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Resolution
        ImGui::Text("Resolution:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200 * S);
        ImGui::Combo("##reso", &resoIdx, resoNames.data(), (int)resoNames.size());

        // Checkboxes row 1
        ImGui::Checkbox("Trail", &cfg.showTrail);
        ImGui::SameLine(); ImGui::Spacing(); ImGui::SameLine();
        ImGui::Checkbox("Hitsounds", &enableHitsounds);
        ImGui::SameLine(); ImGui::Spacing(); ImGui::SameLine();
        ImGui::Checkbox("Fullscreen", &cfg.fullscreen);
        ImGui::SameLine(); ImGui::Spacing(); ImGui::SameLine();
        if (!cfg.fullscreen) ImGui::BeginDisabled();
        ImGui::Checkbox("Exclusive", &exclusiveFS);
        if (!cfg.fullscreen) ImGui::EndDisabled();

        // Checkboxes row 2
        ImGui::Checkbox("Force HS", &forceHS);
        ImGui::SameLine(); ImGui::Spacing(); ImGui::SameLine();
        ImGui::SetNextItemWidth(140 * S);
        ImGui::Combo("##forceHSType", &forceHSIdx, hsTypes.data(), (int)hsTypes.size());
        ImGui::SameLine(); ImGui::Spacing(); ImGui::SameLine();
        ImGui::Checkbox("Legacy Culling", &legacyCulling);
        ImGui::SameLine(); ImGui::Spacing(); ImGui::SameLine();
        ImGui::Checkbox("MSAA", &msaa);
        ImGui::SameLine(); ImGui::Spacing(); ImGui::SameLine();
        ImGui::SetNextItemWidth(130 * S);
        ImGui::Combo("##msaa", &msaaSIdx, msaaNames.data(), (int)msaaNames.size());

        ImGui::Spacing();

        // Track colors
        bool fillOk   = isValidHexColor(fillBuf);
        bool strokeOk = isValidHexColor(strokeBuf);
        bool bgOk     = isValidHexColor(bgBuf);

        ImGui::Text("Fill: #");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80 * S);
        ImGui::InputText("##fill", fillBuf, 7, ImGuiInputTextFlags_CharsUppercase);
        if (!fillOk && fillBuf[0]) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1,0.3f,0.3f,1),"6 hex"); }
        ImGui::SameLine(); ImGui::Spacing(); ImGui::SameLine();
        ImGui::Text("Stroke: #");
        ImGui::SameLine();
        if (autoStroke) ImGui::BeginDisabled();
        ImGui::SetNextItemWidth(80 * S);
        ImGui::InputText("##stroke", strokeBuf, 7, ImGuiInputTextFlags_CharsUppercase);
        if (autoStroke) ImGui::EndDisabled();
        if (!strokeOk && strokeBuf[0]) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1,0.3f,0.3f,1),"6 hex"); }
        ImGui::SameLine();
        ImGui::Checkbox("Auto", &autoStroke);
        ImGui::SameLine(); ImGui::Spacing(); ImGui::SameLine();
        ImGui::Text("BG: #");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80 * S);
        ImGui::InputText("##bg", bgBuf, 7, ImGuiInputTextFlags_CharsUppercase);
        if (!bgOk && bgBuf[0]) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1,0.3f,0.3f,1),"6 hex"); }

        ImGui::Spacing();
        ImGui::Spacing();

        // Auto-calculate stroke from fill when enabled
        if (autoStroke && fillOk) {
            unsigned r,g,b; sscanf(fillBuf,"%02x%02x%02x",&r,&g,&b);
            r=(unsigned)(r*0.5f); g=(unsigned)(g*0.5f); b=(unsigned)(b*0.5f);
            snprintf(strokeBuf,sizeof(strokeBuf),"%02x%02x%02x",r,g,b);
            strokeOk = true;
        }

        bool valid = (levelBuf[0] != '\0') && fillOk && strokeOk && bgOk;

        // Start + Export buttons (centered at bottom)
        ImGui::SetCursorPosY((float)winH - 60 * S);
        float btnW = 120 * S;
        ImGui::SetCursorPosX((float)(winW - (btnW * 2 + 12 * S)) / 2.0f);
        if (!valid) ImGui::BeginDisabled();
        if (ImGui::Button("Start", ImVec2(btnW, 36 * S))) {
            cfg.levelPath        = levelBuf;
            cfg.musicPath        = musicBuf;
            cfg.trackFillColor   = fillBuf;
            cfg.trackStrokeColor = strokeBuf;
            cfg.backgroundColor  = bgBuf;
            cfg.autoStroke       = autoStroke;
            cfg.enableHitsounds  = enableHitsounds;
            cfg.forceHitsoundType = forceHS ? hsTypes[forceHSIdx] : "";
            cfg.legacyCulling    = legacyCulling;
            cfg.msaaSamples      = msaa ? msaaSamplesArr[msaaSIdx] : 0;
            cfg.exclusiveFullscreen = exclusiveFS;
            cfg.resolutionW      = resoW[resoIdx];
            cfg.resolutionH      = resoH[resoIdx];
            cfg.cancelled        = false;
            done = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Export", ImVec2(btnW, 36 * S))) {
            cfg.levelPath        = levelBuf;
            cfg.musicPath        = musicBuf;
            cfg.enableHitsounds  = true;
            cfg.exportHitsounds  = true;
            cfg.cancelled        = false;
            done = true;
        }
        if (!valid) ImGui::EndDisabled();

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
