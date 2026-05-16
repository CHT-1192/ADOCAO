#include "LoadingWindow.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <thread>
#include <cstdio>

static constexpr int LOADING_W = 480;
static constexpr int LOADING_H = 160;

void showLoadingWindow(std::function<void(LoadingProgress&)> loader) {
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
    int winW = (int)(LOADING_W * dpiScale);
    int winH = (int)(LOADING_H * dpiScale);

    // ---- Create window ----
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(winW, winH, "ADOCAO - Loading", nullptr, nullptr);
    if (!window) {
        fprintf(stderr, "Failed to create loading window\n");
        return;
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
    ImGui::StyleColorsDark();

    // DPI-aware sizing: scale all UI elements, load fonts at native resolution
    ImGui::GetStyle().ScaleAllSizes(dpiScale);
    float fontSize = 16.0f * dpiScale;

    // Load CJK font
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
        static const ImWchar cjkRanges[] = {
            0x2000, 0x206F, 0x3000, 0x303F, 0x3040, 0x309F,
            0x30A0, 0x30FF, 0x3130, 0x318F, 0x4E00, 0x9FFF,
            0xAC00, 0xD7AF, 0xFF00, 0xFFEF, 0,
        };
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
        ImFontConfig cfg;
        cfg.MergeMode = true;
        bool loaded = false;
        for (const char* path : cjkFonts) {
            FILE* f = fopen(path, "rb");
            if (f) { fclose(f); io.Fonts->AddFontFromFileTTF(path, fontSize, &cfg, cjkRanges); loaded = true; break; }
        }
    }

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    // ---- Start loader thread ----
    LoadingProgress progress;
    std::thread loaderThread([&]() {
        loader(progress);
        progress.percent.store(100.0f);
    });

    // ---- Render loop ----
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Full-window panel
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)winW, (float)winH));
        ImGui::Begin("Loading", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);

        float S = dpiScale;

        ImGui::Spacing();
        ImGui::Text("Loading...");
        ImGui::Separator();
        ImGui::Spacing();

        float pct = progress.percent.load();
        ImGui::SetNextItemWidth(440 * S);
        ImGui::ProgressBar(pct / 100.0f, ImVec2(0, 24 * S));

        ImGui::Spacing();
        ImGui::Text("%s", progress.stageText);

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

        // Close when loader finishes
        if (pct >= 100.0f && !loaderThread.joinable()) {
            break;
        }
        if (pct >= 100.0f && loaderThread.joinable()) {
            loaderThread.join();
            break;
        }
    }

    // Ensure thread is joined
    if (loaderThread.joinable()) {
        loaderThread.join();
    }

    // ---- Cleanup ----
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
}
