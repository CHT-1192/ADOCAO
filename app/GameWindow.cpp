#include "GameWindow.hpp"
#include "glad/gl_core.hpp"
#include "render/Shader.hpp"
#include "render/Shaders.hpp"
#include "camera/Camera.hpp"
#include "track/TileMesh.hpp"
#include "game/Planet.hpp"
#include "render/PlanetTrail.hpp"
#include "util/Logger.hpp"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <chrono>
#include <thread>

namespace {

struct Viewport { int x=0, y=0, w=0, h=0; };

Viewport computeLetterbox(int fbW, int fbH, float targetAspect) {
    float fbAspect = (float)fbW / (float)fbH;
    Viewport vp;
    if (targetAspect > fbAspect) {
        vp.w = fbW; vp.h = (int)(fbW / targetAspect); vp.x = 0; vp.y = (fbH - vp.h) / 2;
    } else {
        vp.h = fbH; vp.w = (int)(fbH * targetAspect); vp.x = (fbW - vp.w) / 2; vp.y = 0;
    }
    return vp;
}

static void jumpToTile(PlaybackEngine& pb, AudioEngine& audio, HitsoundManager& hs,
                        const LevelData& level, int floor) {
    if (floor < 0 || floor >= (int)level.tiles.size()) return;
    double targetTime = pb.tileStartTimes()[floor];
    float offsetSec = level.settings.offset / 1000.0f;
    float audioPos = (float)(targetTime + offsetSec);
    if (audioPos < 0) audioPos = 0;
    pb.startAt(glfwGetTime(), audioPos, offsetSec);
    hs.resetAt(audioPos);
    if (audio.hasMusic()) { audio.seek(audioPos); audio.play(); }
    else audio.play();
}

// Navigate camera to a tile without starting playback
static void navigateToTile(const LevelData& level, int floor,
                            Camera& camera, double& baseTX, double& baseTY,
                            double& offX, double& offY, int& selTile) {
    if (floor < 0 || floor >= (int)level.tiles.size()) return;
    auto& t = level.tiles[floor];
    camera.setTarget(t.position[0], t.position[1]);
    baseTX = t.position[0]; baseTY = t.position[1];
    offX = 0; offY = 0;
    selTile = floor;
}

} // namespace

bool GameWindow::init(const LauncherConfig& cfg, LoadResult& result) {
    m_cfg = &cfg;
    m_level = result.level.get();
    m_playback = &result.playback;
    m_hitsoundMgr = &result.hitsounds;
    m_audioEngine = &result.audio;
    m_targetAspect = (float)cfg.resolutionW / (float)cfg.resolutionH;
    LOG_D("GameWindow::init: %zu tiles, fullscreen=%d exclusive=%d", m_level->tiles.size(), cfg.fullscreen, cfg.exclusiveFullscreen);
    m_tileVisEnabled = (m_level->settings.trackDisappearAnimation != "None" ||
                        m_level->settings.trackAnimation != "None" ||
                        !m_level->atStates.empty());
    LOG_D("TrackVis: enabled=%d da=%s aa=%s atStates=%zu", m_tileVisEnabled,
          m_level->settings.trackDisappearAnimation.c_str(),
          m_level->settings.trackAnimation.c_str(),
          m_level->atStates.size());

    // Create window
    m_exclusiveFullscreen = cfg.exclusiveFullscreen;
    m_isFullscreen = cfg.fullscreen;
    m_windowedW = cfg.resolutionW;
    m_windowedH = cfg.resolutionH;

    if (cfg.msaaSamples > 0) glfwWindowHint(GLFW_SAMPLES, cfg.msaaSamples);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    GLFWmonitor* primary = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = primary ? glfwGetVideoMode(primary) : nullptr;

    if (cfg.fullscreen) {
        if (cfg.exclusiveFullscreen) {
            // Exclusive fullscreen: GPU dedicated to this app, mode switch
            glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
            m_window = glfwCreateWindow(mode->width, mode->height, "ADOCAO", primary, nullptr);
        } else {
            // Borderless windowed fullscreen: compositor still active
            glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
            m_window = glfwCreateWindow(mode->width, mode->height, "ADOCAO", nullptr, nullptr);
            glfwSetWindowPos(m_window, 0, 0);
        }
    } else {
        glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
        m_window = glfwCreateWindow(cfg.resolutionW, cfg.resolutionH, "ADOCAO", nullptr, nullptr);
        if (mode) {
            glfwSetWindowPos(m_window, (mode->width-cfg.resolutionW)/2, (mode->height-cfg.resolutionH)/2);
            m_windowedX = (mode->width-cfg.resolutionW)/2;
            m_windowedY = (mode->height-cfg.resolutionH)/2;
        }
    }
    if (!m_window) { LOG_E("Failed to create game window"); return false; }

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(0);
    if (!loadGLCore()) { LOG_E("Failed to load OpenGL functions"); glfwDestroyWindow(m_window); return false; }
    if (cfg.msaaSamples > 0) glEnable(GL_MULTISAMPLE);
    LOG_D("OpenGL %s | GLSL %s", glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION));

    // Detect GPU: only NVIDIA has reliable GL context sharing across threads.
    // Intel & AMD iGPUs often fail to share VAO/VBO between contexts → sync build.
    const char* vendor = (const char*)glGetString(GL_VENDOR);
    const char* renderer = (const char*)glGetString(GL_RENDERER);
    LOG_D("GPU vendor=%s renderer=%s", vendor ? vendor : "?", renderer ? renderer : "?");
    if (vendor) {
        std::string v(vendor);
        m_useAsyncBuild = (v.find("NVIDIA") != std::string::npos);
    } else {
        m_useAsyncBuild = false;
    }
    LOG_D("Async build: %s", m_useAsyncBuild ? "ON" : "OFF (sync)");

    // Show window immediately so user sees it while heavy init runs
    glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glfwSwapBuffers(m_window);

    // Shaders (heap-allocated, freed on destruction)
    m_tileShader = new Shader();
    m_planetShader = new Shader();
    m_trailShader = new Shader();
    m_highlightShader = new Shader();

    auto compileShader = [](Shader& s, const char* vp, const char* fp, const char* vs, const char* fs) -> bool {
        if (s.compileFile(vp, fp)) return true;
        LOG_W("Shader file loading failed, using inline fallback");
        return s.compile(vs, fs);
    };
    if (!compileShader(*m_tileShader, "shaders/tile.vert","shaders/tile.frag",Shaders::kTileVertSrc,Shaders::kTileFragSrc)
     || !compileShader(*m_planetShader,"shaders/planet.vert","shaders/planet.frag",Shaders::kPlanetVertSrc,Shaders::kPlanetFragSrc)
     || !compileShader(*m_trailShader,"shaders/trail.vert","shaders/trail.frag",Shaders::kTrailVertSrc,Shaders::kTrailFragSrc)
     || !compileShader(*m_highlightShader,"shaders/highlight.vert","shaders/highlight.frag",Shaders::kHighlightVertSrc,Shaders::kHighlightFragSrc)) {
        LOG_E("Shader compilation failed"); glfwDestroyWindow(m_window); return false;
    }

    // Track
    m_tileMesh = new TileMesh();

    if (m_useAsyncBuild) {
        // Async: build on shared GL context in background thread
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        glfwWindowHint(GLFW_SAMPLES, 0);
        m_sharedWindow = glfwCreateWindow(1, 1, "buildctx", nullptr, m_window);
        if (m_sharedWindow) {
            LOG_D("Spawning background mesh build thread");
            m_buildFuture = std::async(std::launch::async, [this, &cfg]() {
                glfwMakeContextCurrent(m_sharedWindow);
                m_tileMesh->build(*m_level, cfg.trackFillColor, cfg.trackStrokeColor, cfg.legacyCulling);
                if (m_playback->redPlanet()) {
                    m_playback->redPlanet()->buildGPU();
                    m_playback->bluePlanet()->buildGPU();
                }
                glfwMakeContextCurrent(nullptr);
            });
        } else {
            LOG_W("Shared context creation failed, falling back to sync build");
            m_useAsyncBuild = false;
        }
    }

    if (!m_useAsyncBuild) {
        // Sync: build on main thread (window already visible)
        m_tileMesh->build(*m_level, cfg.trackFillColor, cfg.trackStrokeColor, cfg.legacyCulling);
        std::vector<double>().swap(m_level->angleData);
        m_level->tileBPMs.clear(); m_level->tileBPMs.shrink_to_fit();
        m_level->tileHasTwirl.clear(); m_level->tileHasTwirl.shrink_to_fit();
        m_level->tileHasSetSpeed.clear(); m_level->tileHasSetSpeed.shrink_to_fit();
        if (m_playback->redPlanet()) { m_playback->redPlanet()->buildGPU(); m_playback->bluePlanet()->buildGPU(); }
        m_meshReady = true;
    }
    // Camera + background
    {
        std::string hex = cfg.backgroundColor;
        if (hex.length()>=6) { unsigned r,g,b; sscanf(hex.c_str(),"%02x%02x%02x",&r,&g,&b);
            m_bgR=r/255.0f; m_bgG=g/255.0f; m_bgB=b/255.0f; }
    }
    m_camera.setZoom(m_level->settings.zoom);
    if (!m_level->tiles.empty()) { auto& t = m_level->tiles[0];
        m_camera.setTarget(t.position[0], t.position[1]);
        m_input.baseTargetX = t.position[0]; m_input.baseTargetY = t.position[1];
    }
    m_input.camera = &m_camera;

    // Hitsound attach
    if (m_hitsoundMgr->isSynthesized()) {
        m_audioEngine->attachExternal(m_hitsoundMgr->buffer(), m_hitsoundMgr->totalFrames(),
            m_hitsoundMgr->channels(), m_hitsoundMgr->sampleRate(),
            m_hitsoundMgr->cursor(), m_hitsoundMgr->playing());
    }

    // Input callbacks
    glfwSetWindowUserPointer(m_window, &m_input);
    glfwSetMouseButtonCallback(m_window, [](GLFWwindow* w, int b, int a, int) {
        auto* in = static_cast<Input*>(glfwGetWindowUserPointer(w));
        if (b == GLFW_MOUSE_BUTTON_LEFT) {
            if (a == GLFW_PRESS) { in->dragActive=true; in->dragStartX=in->cursorX; in->dragStartY=in->cursorY; }
            else { double dx=in->cursorX-in->dragStartX, dy=in->cursorY-in->dragStartY;
                in->dragActive=false; in->baseTargetX+=in->offsetX; in->baseTargetY+=in->offsetY;
                in->offsetX=0; in->offsetY=0; if (dx*dx+dy*dy < 25.0) in->justClicked = true; }
        }
    });
    glfwSetCursorPosCallback(m_window, [](GLFWwindow* w, double x, double y) {
        auto* in = static_cast<Input*>(glfwGetWindowUserPointer(w)); in->cursorX=x; in->cursorY=y;
    });
    glfwSetScrollCallback(m_window, [](GLFWwindow* w, double, double dy) {
        auto* in = static_cast<Input*>(glfwGetWindowUserPointer(w));
        float minZoom = ADOCAO_MIN_ZOOM, maxZoom = 1000.0f;
        float z = in->camera->zoom() * (1.0f + (float)dy * 0.1f);
        if (z<minZoom)z=minZoom; if (z>maxZoom)z=maxZoom;
        in->camera->setZoom(z);
    });

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    m_lastFrameTime = glfwGetTime();
    m_autoPlayTriggerTime = glfwGetTime() + (cfg.autoPlay ? 0.5 : 999999.0);
    return true;
}

void GameWindow::handleInput() {
    if (glfwGetKey(m_window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(m_window, GLFW_TRUE);

    // Alt+Enter toggles fullscreen
    bool altEnter = (glfwGetKey(m_window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS
                  || glfwGetKey(m_window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS)
                 && glfwGetKey(m_window, GLFW_KEY_ENTER) == GLFW_PRESS;
    if (altEnter && !m_wasAltEnterPressed) {
        toggleFullscreen();
    }
    m_wasAltEnterPressed = altEnter;

    double now = glfwGetTime();

    // Space toggles playback (or auto-play trigger)
    bool spacePressed = (glfwGetKey(m_window, GLFW_KEY_SPACE) == GLFW_PRESS)
                     || (m_cfg->autoPlay && now >= m_autoPlayTriggerTime && !m_playback->isPlaying());
    if (spacePressed && !m_wasSpacePressed) {
        m_autoPlayTriggerTime = 999999.0;
        if (!m_playback->isPlaying()) {
            float offsetSec = m_level->settings.offset / 1000.0f;
            if (m_input.selectedTile >= 0) {
                double targetTime = m_playback->tileStartTimes()[m_input.selectedTile];
                float audioPos = (float)(targetTime + offsetSec);
                if (audioPos < 0) audioPos = 0;
                m_playback->startAt(glfwGetTime(), audioPos, offsetSec);
                m_hitsoundMgr->resetAt(audioPos);
                if (m_audioEngine->hasMusic()) { m_audioEngine->seek(audioPos); m_audioEngine->play(); }
                else m_audioEngine->play();
                m_input.selectedTile = -1;
            } else {
                m_playback->start(glfwGetTime());
                m_hitsoundMgr->resetAt(0);
                m_musicPending = true;  // always delay audio start by audioStartOffset (pre-roll)
            }
        } else { m_playback->stop(); m_audioEngine->pause(); m_musicPending = false; }
    }
    m_wasSpacePressed = spacePressed;

    // Click-to-select tile (only when stopped)
    if (!m_playback->isPlaying() && m_input.justClicked) {
        m_input.justClicked = false;
        int fbW, fbH, winW, winH;
        glfwGetFramebufferSize(m_window, &fbW, &fbH);
        glfwGetWindowSize(m_window, &winW, &winH);
        Viewport vp2 = computeLetterbox(fbW, fbH, m_targetAspect);
        double halfH = 6.0/(m_camera.zoom()/100.0);
        double halfW = halfH*(double)vp2.w/(double)vp2.h;
        double pxToWorldX = (2.0*halfW)/(double)vp2.w;
        double pxToWorldY = (2.0*halfH)/(double)vp2.h;
        double worldX = m_camera.targetX() + (m_input.cursorX - vp2.x)*pxToWorldX - halfW;
        double worldY = m_camera.targetY() - (m_input.cursorY - vp2.y)*pxToWorldY + halfH;

        int best = -1; double bestDist = 1.0;
        for (int i = 0; i < (int)m_level->tiles.size()-1; i++) {
            double dx = m_level->tiles[i].position[0] - worldX;
            double dy = m_level->tiles[i].position[1] - worldY;
            double d = dx*dx + dy*dy;
            if (d < bestDist*bestDist) { bestDist = std::sqrt(d); best = i; }
        }
        m_input.selectedTile = best;
        if (best >= 0) navigateToTile(*m_level, best, m_camera,
            m_input.baseTargetX, m_input.baseTargetY,
            m_input.offsetX, m_input.offsetY, m_input.selectedTile);
    }

    // Bookmark navigation: Ctrl+Left/Right with long-press repeat (only when stopped)
    if (!m_playback->isPlaying() && !m_level->bookmarkFloors.empty()) {
        static double bmLHoldStart = 0, bmRHoldStart = 0;
        bool bmL=(glfwGetKey(m_window,GLFW_KEY_LEFT_CONTROL)==GLFW_PRESS || glfwGetKey(m_window,GLFW_KEY_RIGHT_CONTROL)==GLFW_PRESS)
              && (glfwGetKey(m_window,GLFW_KEY_LEFT)==GLFW_PRESS);
        bool bmR=(glfwGetKey(m_window,GLFW_KEY_LEFT_CONTROL)==GLFW_PRESS || glfwGetKey(m_window,GLFW_KEY_RIGHT_CONTROL)==GLFW_PRESS)
              && (glfwGetKey(m_window,GLFW_KEY_RIGHT)==GLFW_PRESS);
        double now = glfwGetTime();
        auto jumpBM = [&](bool left) {
            int cur=m_input.selectedTile, target=-1;
            if (left) { for (int b : m_level->bookmarkFloors) { if (b<cur) target=b; else break; } }
            else      { for (int b : m_level->bookmarkFloors) { if (b>cur) { target=b; break; } } }
            if (target>=0) navigateToTile(*m_level,target,m_camera,m_input.baseTargetX,m_input.baseTargetY,m_input.offsetX,m_input.offsetY,m_input.selectedTile);
        };
        if (bmL) {
            if (bmLHoldStart == 0) { bmLHoldStart = now; jumpBM(true); }
            else if (now - bmLHoldStart >= 0.5) { bmLHoldStart = now; jumpBM(true); }
        } else { bmLHoldStart = 0; }
        if (bmR) {
            if (bmRHoldStart == 0) { bmRHoldStart = now; jumpBM(false); }
            else if (now - bmRHoldStart >= 0.5) { bmRHoldStart = now; jumpBM(false); }
        } else { bmRHoldStart = 0; }
    }

    // Arrow key tile navigation: long-press with 0.5s initial delay (only when stopped, tile selected, Ctrl NOT held)
    bool ctrlHeld = (glfwGetKey(m_window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
                 || (glfwGetKey(m_window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS);
    if (!m_playback->isPlaying() && m_input.selectedTile >= 0 && !ctrlHeld) {
        static double arrowHoldStart = 0;
        bool al=(glfwGetKey(m_window,GLFW_KEY_LEFT)==GLFW_PRESS);
        bool ar=(glfwGetKey(m_window,GLFW_KEY_RIGHT)==GLFW_PRESS);
        int tn=(int)m_level->tiles.size()-1;
        if (al || ar) {
            double now = glfwGetTime();
            bool firstPress = (arrowHoldStart == 0);
            if (firstPress) arrowHoldStart = now;
            bool move = firstPress || (now - arrowHoldStart >= 0.5);
            if (move) {
                if (al && m_input.selectedTile > 0) m_input.selectedTile--;
                if (ar && m_input.selectedTile < tn - 1) m_input.selectedTile++;
                navigateToTile(*m_level, m_input.selectedTile, m_camera,
                    m_input.baseTargetX, m_input.baseTargetY,
                    m_input.offsetX, m_input.offsetY, m_input.selectedTile);
            }
        } else {
            arrowHoldStart = 0;
        }
    }
}

void GameWindow::update(float) {
    double now = glfwGetTime();

    // Playback update
    if (m_playback->isPlaying()) {
        // Delayed music start: wait for audioStartOffset before playing from position 0
        if (m_musicPending && m_playback->elapsedTimeMs() >= m_playback->audioStartOffset() * 1000.0f) {
            m_musicPending = false;
            if (m_audioEngine->hasMusic()) {
                m_audioEngine->seek(0);
            }
            m_audioEngine->play();
        }
        if (m_audioEngine->hasMusic() && m_audioEngine->isPlaying()) {
            m_playback->syncToAudio(m_audioEngine->position(), m_level->settings.offset/1000.0f);
        } else {
            m_playback->updateWallClock(now);
        }
    }

    // Camera follow during playback
    if (m_playback->isPlaying()) {
        int tileIdx = m_playback->currentTileIndex();
        if (tileIdx >= 0 && tileIdx < (int)m_level->tiles.size()) {
            auto& p = m_level->tiles[tileIdx].position;
            m_camera.setTarget(p[0], p[1]);
            m_input.baseTargetX = p[0]; m_input.baseTargetY = p[1];
            m_input.offsetX = 0; m_input.offsetY = 0;
        }
    }

    // Drag (only when not playing)
    if (!m_playback->isPlaying() && m_input.dragActive) {
        int fbW, fbH, winW, winH;
        glfwGetFramebufferSize(m_window, &fbW, &fbH);
        glfwGetWindowSize(m_window, &winW, &winH);
        Viewport vp = computeLetterbox(fbW, fbH, m_targetAspect);
        if (vp.w>0 && vp.h>0) {
            double halfH = 6.0/(m_camera.zoom()/100.0);
            double halfW = halfH*(double)vp.w/(double)vp.h;
            double pxToWorldX = (2.0*halfW)/(double)vp.w;
            double pxToWorldY = (2.0*halfH)/(double)vp.h;
            m_input.offsetX = -(m_input.cursorX - m_input.dragStartX)*pxToWorldX;
            m_input.offsetY =  (m_input.cursorY - m_input.dragStartY)*pxToWorldY;
        }
    }
    if (!m_playback->isPlaying()) {
        m_camera.setTarget(m_input.baseTargetX + m_input.offsetX, m_input.baseTargetY + m_input.offsetY);
    }
}

void GameWindow::render() {
    int fbW, fbH, winW, winH;
    glfwGetFramebufferSize(m_window, &fbW, &fbH);
    glfwGetWindowSize(m_window, &winW, &winH);
    Viewport vp = computeLetterbox(fbW, fbH, m_targetAspect);

    glViewport(0, 0, fbW, fbH);
    glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);

    glViewport(vp.x, vp.y, vp.w, vp.h);
    glScissor(vp.x, vp.y, vp.w, vp.h);
    glEnable(GL_SCISSOR_TEST);
    glClearColor(m_bgR, m_bgG, m_bgB, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);

    m_camera.setAspect((float)vp.w, (float)vp.h);

    // Track disappear animation: per-tile check in delta range
    if (m_tileVisEnabled) {
        const auto& dt = m_playback->tileDisappearTimes();
        const auto& at = m_playback->tileAppearTimes();
        int n = (int)dt.size();
        if (n > 0 && m_playback->isPlaying()) {
            double t = m_playback->timeInLevel();
            // Find approximate range: last tile with finite disappearTime <= t
            int lo = 0, hi = n - 1, rangeEnd = -1;
            while (lo <= hi) {
                int m = (lo + hi) / 2;
                if (dt[m] <= t) { rangeEnd = m; lo = m + 1; }
                else hi = m - 1;
            }
            if (rangeEnd != m_lastHiddenEnd) {
                int start = std::min(m_lastHiddenEnd, rangeEnd) + 1;
                int end   = std::max(m_lastHiddenEnd, rangeEnd);
                for (int i = start; i <= end; i++) {
                    bool hide = (dt[i] <= t) || (at[i] > t);
                    m_tileMesh->updateVisibleRange(i, i, !hide);
                }
                m_tileMesh->setVisibleThreshold(rangeEnd);
                m_lastHiddenEnd = rangeEnd;
            }
            if (rangeEnd >= 0) m_sgVisibleLatch = true;
        }
        // On pause/stop: restore all hidden tiles to visible
        if (!m_playback->isPlaying() && m_sgVisibleLatch) {
            m_tileMesh->updateVisibleRange(0, std::max(0, m_lastHiddenEnd), true);
            m_tileMesh->setVisibleThreshold(-1);
            m_lastHiddenEnd = -1;
            m_sgVisibleLatch = false;
        }
    }

    // Tiles
    m_tileShader->use();
    m_tileShader->setMat4("uVP", glm::value_ptr(m_camera.viewProj()));
    float vl, vr, vb, vt; m_camera.frustumBounds(vl, vr, vb, vt);
    m_tileMesh->draw(vl, vr, vb, vt, m_camera.targetX(), m_camera.targetY());

    // Trails
    if (m_playback->isPlaying() && m_playback->redPlanet() && m_playback->redPlanet()->trail) {
        m_playback->redPlanet()->trail->draw(*m_trailShader, m_camera, m_camera.targetX(), m_camera.targetY());
        m_playback->bluePlanet()->trail->draw(*m_trailShader, m_camera, m_camera.targetX(), m_camera.targetY());
    }

    // Planets
    if (m_playback->isPlaying() && m_playback->redPlanet() && m_playback->redPlanet()->gpuBuilt()) {
        m_playback->redPlanet()->draw(*m_planetShader, m_camera, m_camera.targetX(), m_camera.targetY());
        m_playback->bluePlanet()->draw(*m_planetShader, m_camera, m_camera.targetX(), m_camera.targetY());
    }

    // Icons
    m_tileShader->use();
    m_tileMesh->drawIcons(vl, vr, vb, vt, m_camera.targetX(), m_camera.targetY());

    // Highlight
    if (!m_playback->isPlaying() && m_input.selectedTile >= 0) {
        glDisable(GL_DEPTH_TEST);
        m_highlightShader->use();
        m_highlightShader->setMat4("uVP", glm::value_ptr(m_camera.viewProj()));
        m_tileMesh->drawHighlightedTile(m_input.selectedTile, m_camera.targetX(), m_camera.targetY());
        glEnable(GL_DEPTH_TEST);
    }

    glfwSwapBuffers(m_window);
}

void GameWindow::toggleFullscreen() {
    if (m_isFullscreen) {
        // Go windowed
        glfwSetWindowMonitor(m_window, nullptr,
            m_windowedX, m_windowedY, m_windowedW, m_windowedH, 0);
        m_isFullscreen = false;
    } else {
        // Save windowed position and size
        glfwGetWindowPos(m_window, &m_windowedX, &m_windowedY);
        glfwGetWindowSize(m_window, &m_windowedW, &m_windowedH);

        GLFWmonitor* primary = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(primary);
        if (!mode) return;

        glfwSetWindowMonitor(m_window, primary, 0, 0, mode->width, mode->height, GLFW_DONT_CARE);
        m_isFullscreen = true;
    }
}

void GameWindow::run() {
    double targetFrameTime = 1.0 / 320.0;

    while (!glfwWindowShouldClose(m_window)) {
        glfwPollEvents();
        handleInput();

        // Check async build completion (NVIDIA/AMD only)
        if (!m_meshReady && m_buildFuture.valid()) {
            if (m_buildFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                m_buildFuture.get();
                m_meshReady = true;
                if (m_sharedWindow) {
                    glfwDestroyWindow(m_sharedWindow);
                    m_sharedWindow = nullptr;
                }
                std::vector<double>().swap(m_level->angleData);
                m_level->tileBPMs.clear(); m_level->tileBPMs.shrink_to_fit();
                m_level->tileHasTwirl.clear(); m_level->tileHasTwirl.shrink_to_fit();
                m_level->tileHasSetSpeed.clear(); m_level->tileHasSetSpeed.shrink_to_fit();
                LOG_D("Async mesh build complete");
            }
        }

        // Frame pacing
        double now = glfwGetTime();
        double elapsed = now - m_lastFrameTime;
        if (elapsed < targetFrameTime && elapsed > 0) {
            double remaining = targetFrameTime - elapsed;
            if (remaining > 0.002)
                std::this_thread::sleep_for(std::chrono::duration<double>(remaining - 0.001));
            while ((now = glfwGetTime()) < m_lastFrameTime + targetFrameTime) {}
            elapsed = targetFrameTime;
        }
        float deltaMs = (float)(elapsed * 1000.0);
        m_lastFrameTime = now;
        if (deltaMs > 500.0f) deltaMs = 0.0f;
        else if (deltaMs > 100.0f) deltaMs = 100.0f;

        if (m_meshReady) {
            update(deltaMs);
            render();
        } else {
            // Still building async: show background color, keep responsive
            int fbW, fbH;
            glfwGetFramebufferSize(m_window, &fbW, &fbH);
            glViewport(0, 0, fbW, fbH);
            glClearColor(m_bgR, m_bgG, m_bgB, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glfwSwapBuffers(m_window);
        }
    }

    m_audioEngine->shutdown();
    // Cleanup heap-allocated objects
    delete m_tileMesh;
    delete m_tileShader;
    delete m_planetShader;
    delete m_trailShader;
    delete m_highlightShader;
    glfwDestroyWindow(m_window);
}

void showGameWindow(const LauncherConfig& cfg, LoadResult& result) {
    GameWindow gw;
    if (gw.init(cfg, result)) gw.run();
}
