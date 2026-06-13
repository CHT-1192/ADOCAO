#include "GameWindow.h"
#include "glad/gl_core.h"
#include "render/Shader.h"
#include "render/Shaders.h"
#include "camera/Camera.h"
#include "track/TileMesh.h"
#include "game/Planet.h"
#include "render/PlanetTrail.h"
#include "util/Logger.h"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <chrono>
#include <thread>

namespace {

struct GameInput {
    bool   dragActive = false;
    double dragStartX = 0.0;
    double dragStartY = 0.0;
    double cursorX    = 0.0;
    double cursorY    = 0.0;
    double baseTargetX = 0.0;
    double baseTargetY = 0.0;
    double offsetX = 0.0;
    double offsetY = 0.0;
    Camera* camera = nullptr;
};

struct Viewport { int x=0, y=0, w=0, h=0; };

Viewport computeLetterbox(int fbW, int fbH, float targetAspect) {
    float fbAspect = (float)fbW / (float)fbH;
    Viewport vp;
    if (targetAspect > fbAspect) {
        vp.w = fbW;
        vp.h = (int)(fbW / targetAspect);
        vp.x = 0;
        vp.y = (fbH - vp.h) / 2;
    } else {
        vp.h = fbH;
        vp.w = (int)(fbH * targetAspect);
        vp.x = (fbW - vp.w) / 2;
        vp.y = 0;
    }
    return vp;
}

} // namespace

void showGameWindow(const LauncherConfig& cfg, LoadResult& result) {
    auto& level = result.level;
    auto& playback = result.playback;
    auto& hitsoundMgr = result.hitsounds;
    auto& audioEngine = result.audio;

    GLFWmonitor* targetMonitor = cfg.fullscreen ? glfwGetPrimaryMonitor() : nullptr;
    float targetAspect = (float)cfg.resolutionW / (float)cfg.resolutionH;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);

    GLFWwindow* window;
    if (targetMonitor) {
        const GLFWvidmode* mode = glfwGetVideoMode(targetMonitor);
        window = glfwCreateWindow(mode->width, mode->height, "ADOCAO", targetMonitor, nullptr);
    } else {
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

    if (!window) { LOG_E("Failed to create game window"); return; }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);  // disable vsync for max framerate

    if (!loadGLCore()) { LOG_E("Failed to load OpenGL functions"); glfwDestroyWindow(window); return; }

    LOG_I("OpenGL %s | GLSL %s", glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION));

    // ---- Shaders ----
    Shader tileShader;
    if (!tileShader.compile(Shaders::kTileVertSrc, Shaders::kTileFragSrc)) {
        LOG_E("Tile shader failed"); glfwDestroyWindow(window); return;
    }
    Shader planetShader;
    if (!planetShader.compile(Shaders::kPlanetVertSrc, Shaders::kPlanetFragSrc)) {
        LOG_E("Planet shader failed"); glfwDestroyWindow(window); return;
    }
    Shader trailShader;
    if (!trailShader.compile(Shaders::kTrailVertSrc, Shaders::kTrailFragSrc)) {
        LOG_E("Trail shader failed"); glfwDestroyWindow(window); return;
    }

    // Compute shader (GPU culling, OpenGL 4.3+)
    Shader tileOffsetShader;
    bool gpuCullAvail = playback.gpuCulling() && tileOffsetShader.compileCompute(Shaders::kTileOffsetCompSrc);
    if (!gpuCullAvail && playback.gpuCulling())
        LOG_I("GPU culling unavailable, using CPU path");

    // ---- Track ----
    TileMesh tileMesh;
    tileMesh.build(*level, cfg.trackFillColor, cfg.trackStrokeColor);

    // ---- Camera ----
    Camera camera;
    GameInput input; input.camera = &camera;
    float bgR=0, bgG=0, bgB=0;
    {
        std::string hex = cfg.backgroundColor;
        if (hex.length()>=6) {
            unsigned r,g,b; sscanf(hex.c_str(),"%02x%02x%02x",&r,&g,&b);
            bgR=r/255.0f;bgG=g/255.0f;bgB=b/255.0f;
        }
    }
    camera.setZoom(level->settings.zoom);
    if (!level->tiles.empty()) {
        auto& t = level->tiles[0];
        camera.setTarget(t.position[0], t.position[1]);
        input.baseTargetX = t.position[0];
        input.baseTargetY = t.position[1];
    }

    // ---- Build planet GPU resources ----
    if (playback.redPlanet()) {
        playback.redPlanet()->buildGPU();
        playback.bluePlanet()->buildGPU();
    }

    // Attach pre-synthesized hitsound buffer
    if (hitsoundMgr.isSynthesized()) {
        audioEngine.attachExternal(hitsoundMgr.buffer(), hitsoundMgr.totalFrames(),
                                   hitsoundMgr.channels(), hitsoundMgr.sampleRate(),
                                   hitsoundMgr.cursor(), hitsoundMgr.playing());
    }

    // ---- Input callbacks ----
    glfwSetMouseButtonCallback(window, [](GLFWwindow* w, int button, int action, int) {
        auto* in = static_cast<GameInput*>(glfwGetWindowUserPointer(w));
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            if (action == GLFW_PRESS) {
                in->dragActive=true; in->dragStartX=in->cursorX; in->dragStartY=in->cursorY;
            } else {
                in->dragActive=false; in->baseTargetX+=in->offsetX; in->baseTargetY+=in->offsetY;
                in->offsetX=0; in->offsetY=0;
            }
        }
    });
    glfwSetCursorPosCallback(window, [](GLFWwindow* w, double x, double y) {
        auto* in = static_cast<GameInput*>(glfwGetWindowUserPointer(w));
        in->cursorX=x; in->cursorY=y;
    });
    glfwSetScrollCallback(window, [](GLFWwindow* w, double, double dy) {
        auto* in = static_cast<GameInput*>(glfwGetWindowUserPointer(w));
        float z = in->camera->zoom() * (1.0f + (float)dy * 0.1f);
        if (z<5)z=5; if (z>1000)z=1000;
        in->camera->setZoom(z);
    });
    glfwSetWindowUserPointer(window, &input);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    // ---- Main loop ----
    double lastFrameTime = glfwGetTime();
    constexpr double targetFrameTime = 1.0 / 320.0;  // 320 FPS soft cap
    bool wasSpacePressed = false;
    double autoPlayTriggerTime = glfwGetTime() + (cfg.autoPlay ? 0.5 : 999999.0);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, GLFW_TRUE);

        // Delta time with sleep + spin cap (CPU-efficient)
        double now = glfwGetTime();
        double elapsed = now - lastFrameTime;
        if (elapsed < targetFrameTime && elapsed > 0) {
            double remaining = targetFrameTime - elapsed;
            if (remaining > 0.002) {
                std::this_thread::sleep_for(
                    std::chrono::duration<double>(remaining - 0.001));
            }
            while ((now = glfwGetTime()) < lastFrameTime + targetFrameTime) {
                // Spin last ~1ms for precision
            }
            elapsed = targetFrameTime;
        }
        float deltaMs = (float)(elapsed * 1000.0);
        lastFrameTime = now;
        if (deltaMs > 500.0f) deltaMs = 0.0f;
        else if (deltaMs > 100.0f) deltaMs = 100.0f;

        // Space toggles playback (or auto-play trigger)
        bool spacePressed = (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
                         || (cfg.autoPlay && now >= autoPlayTriggerTime && !playback.isPlaying());
        if (spacePressed && !wasSpacePressed) {
            autoPlayTriggerTime = 999999.0;  // only fire once
            if (!playback.isPlaying()) {
                playback.start(glfwGetTime());

                const auto& bpmArr = playback.tileBPMPerTile();
                float bpm = bpmArr.size() > 0 ? bpmArr[0] : level->settings.bpm;
                float offsetSec = level->settings.offset / 1000.0f;

                // Reset hitsounds cursor for new playback
                hitsoundMgr.reset();

                if (audioEngine.hasMusic()) {
                    audioEngine.seek(offsetSec);
                    audioEngine.play();
                } else {
                    audioEngine.play();
                }
            } else {
                playback.stop();
                audioEngine.pause();
            }
        }
        wasSpacePressed = spacePressed;

        // Update playback — sync to audio clock when music playing, else wall-clock
        if (playback.isPlaying()) {
            if (audioEngine.hasMusic() && audioEngine.isPlaying()) {
                float offsetSec = level->settings.offset / 1000.0f;
                playback.syncToAudio(audioEngine.position(), offsetSec);
            } else {
                playback.updateWallClock(now);
            }
        }

        // Camera: follow pivot during playback, allow drag when stopped
        if (playback.isPlaying()) {
            int tileIdx = playback.currentTileIndex();
            int pivotIdx = (tileIdx >= 0) ? tileIdx : 0;
            if (pivotIdx < (int)level->tiles.size()) {
                auto& p = level->tiles[pivotIdx].position;
                camera.setTarget(p[0], p[1]);
                input.baseTargetX = p[0];
                input.baseTargetY = p[1];
                input.offsetX = 0;
                input.offsetY = 0;
            }
        }

        // Letterbox viewport
        int fbW, fbH, winW, winH;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        glfwGetWindowSize(window, &winW, &winH);
        Viewport vp = computeLetterbox(fbW, fbH, targetAspect);

        // Drag (only when not playing)
        if (!playback.isPlaying() && input.dragActive && vp.w>0 && vp.h>0) {
            double halfH = 6.0/(camera.zoom()/100.0);
            double halfW = halfH*(double)vp.w/(double)vp.h;
            double pxToWorldX = (2.0*halfW)/(double)vp.w;
            double pxToWorldY = (2.0*halfH)/(double)vp.h;
            input.offsetX = -(input.cursorX-input.dragStartX)*pxToWorldX;
            input.offsetY =  (input.cursorY-input.dragStartY)*pxToWorldY;
        }
        if (!playback.isPlaying()) {
            camera.setTarget(input.baseTargetX+input.offsetX, input.baseTargetY+input.offsetY);
        }

        glViewport(0, 0, fbW, fbH);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        glViewport(vp.x, vp.y, vp.w, vp.h);
        glScissor(vp.x, vp.y, vp.w, vp.h);
        glEnable(GL_SCISSOR_TEST);
        glClearColor(bgR, bgG, bgB, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);

        camera.setAspect((float)vp.w, (float)vp.h);

        // Draw tiles (no depth test — all visible regardless of Z)
        glDisable(GL_DEPTH_TEST);
        tileShader.use();
        tileShader.setMat4("uVP", glm::value_ptr(camera.viewProj()));
        float vl,vr,vb,vt; camera.frustumBounds(vl,vr,vb,vt);
        tileMesh.draw(vl, vr, vb, vt, camera.targetX(), camera.targetY(),
                      gpuCullAvail, tileOffsetShader.id());
        glEnable(GL_DEPTH_TEST);

        // Draw trails behind planets (no depth test)
        if (playback.isPlaying() && playback.redPlanet() && playback.redPlanet()->trail) {
            glDisable(GL_DEPTH_TEST);
            playback.redPlanet()->trail->draw(trailShader, camera, camera.targetX(), camera.targetY());
            playback.bluePlanet()->trail->draw(trailShader, camera, camera.targetX(), camera.targetY());
            glEnable(GL_DEPTH_TEST);
        }

        // Draw planets on top
        if (playback.isPlaying() && playback.redPlanet() && playback.redPlanet()->gpuBuilt()) {
            playback.redPlanet()->draw(planetShader, camera, camera.targetX(), camera.targetY());
            playback.bluePlanet()->draw(planetShader, camera, camera.targetX(), camera.targetY());
        }

        // Draw event icons on top of everything (no depth test)
        glDisable(GL_DEPTH_TEST);
        tileShader.use();
        tileMesh.drawIcons(vl, vr, vb, vt, camera.targetX(), camera.targetY());
        glEnable(GL_DEPTH_TEST);

        glfwSwapBuffers(window);
    }

    // Cleanup
    audioEngine.shutdown();
    glfwDestroyWindow(window);
}
