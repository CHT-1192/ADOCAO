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
    // ---- Create window ----
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(LOADING_W, LOADING_H, "ADOCAO - Loading", nullptr, nullptr);
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
                (mode->width  - LOADING_W) / 2,
                (mode->height - LOADING_H) / 2);
        }
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    // ---- Init ImGui ----
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    // Load CJK font
    {
        ImGuiIO& io = ImGui::GetIO();
        const char* cjkFonts[] = {
            "C:/Windows/Fonts/msyh.ttc",
            "C:/Windows/Fonts/malgun.ttf",
            "C:/Windows/Fonts/msgothic.ttc",
            "C:/Windows/Fonts/simhei.ttf",
            "C:/Windows/Fonts/gulim.ttc",
        };
        static const ImWchar cjkRanges[] = {
            0x0020, 0x00FF, 0x2000, 0x206F, 0x3000, 0x303F,
            0x3040, 0x309F, 0x30A0, 0x30FF, 0x3130, 0x318F,
            0x4E00, 0x9FFF, 0xAC00, 0xD7AF, 0xFF00, 0xFFEF, 0,
        };
        bool loaded = false;
        for (const char* path : cjkFonts) {
            FILE* f = fopen(path, "rb");
            if (f) { fclose(f); io.Fonts->AddFontFromFileTTF(path, 16.0f, nullptr, cjkRanges); loaded = true; break; }
        }
        if (!loaded) io.Fonts->AddFontDefault();
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
        ImGui::SetNextWindowSize(ImVec2((float)LOADING_W, (float)LOADING_H));
        ImGui::Begin("Loading", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);

        ImGui::Spacing();
        ImGui::Text("Loading...");
        ImGui::Separator();
        ImGui::Spacing();

        float pct = progress.percent.load();
        ImGui::SetNextItemWidth(440);
        ImGui::ProgressBar(pct / 100.0f, ImVec2(0, 24));

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
