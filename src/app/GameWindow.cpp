#include "GameWindow.h"
#include "../glad/gl_core.h"
#include "../render/Shader.h"
#include "../camera/Camera.h"
#include "../track/TileMesh.h"
#include "../util/Logger.h"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

static const char* kVertSrc = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
uniform mat4 uMVP;
out vec3 vColor;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vColor = aColor;
}
)";

static const char* kFragSrc = R"(
#version 330 core
in vec3 vColor;
out vec4 fragColor;
void main() {
    fragColor = vec4(vColor, 1.0);
}
)";

void showGameWindow(const LauncherConfig& cfg, std::unique_ptr<LevelData> level) {
    // Create window
    GLFWmonitor* targetMonitor = cfg.fullscreen ? glfwGetPrimaryMonitor() : nullptr;

    GLFWwindow* window;
    if (targetMonitor) {
        const GLFWvidmode* mode = glfwGetVideoMode(targetMonitor);
        window = glfwCreateWindow(mode->width, mode->height, "ADOCAO", targetMonitor, nullptr);
    } else {
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
        window = glfwCreateWindow(cfg.resolutionW, cfg.resolutionH, "ADOCAO", nullptr, nullptr);

        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        if (monitor) {
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);
            if (mode) {
                glfwSetWindowPos(window,
                    (mode->width  - cfg.resolutionW) / 2,
                    (mode->height - cfg.resolutionH) / 2);
            }
        }
    }

    if (!window) {
        LOG_E("Failed to create game window");
        return;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!loadGLCore()) {
        LOG_E("Failed to load OpenGL functions");
        glfwDestroyWindow(window);
        return;
    }

    LOG_I("OpenGL %s | GLSL %s", glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION));

    // Compile shader
    Shader shader;
    if (!shader.compile(kVertSrc, kFragSrc)) {
        LOG_E("Shader compilation failed");
        glfwDestroyWindow(window);
        return;
    }

    // Build track mesh
    TileMesh tileMesh;
    tileMesh.build(*level);

    // Camera
    Camera camera;
    float bgR = 0.0f, bgG = 0.0f, bgB = 0.0f;
    {
        auto& hex = level->settings.backgroundColor;
        if (hex.length() >= 6) {
            unsigned int r, g, b;
            sscanf(hex.c_str(), "%02x%02x%02x", &r, &g, &b);
            bgR = r / 255.0f; bgG = g / 255.0f; bgB = b / 255.0f;
        }
    }

    camera.setZoom(level->settings.zoom);
    if (!level->tiles.empty()) {
        auto& t = level->tiles[0];
        camera.setTarget(t.position[0], t.position[1]);
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Main render loop
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, GLFW_TRUE);

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        camera.setAspect((float)w, (float)h);

        glViewport(0, 0, w, h);
        glClearColor(bgR, bgG, bgB, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        shader.use();
        shader.setMat4("uMVP", glm::value_ptr(camera.viewProj()));
        tileMesh.draw();
        glfwSwapBuffers(window);
    }

    glfwDestroyWindow(window);
}
