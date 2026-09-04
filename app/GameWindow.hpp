#pragma once

#include "LauncherWindow.hpp"
#include "LevelLoader.hpp"
#include "render/Camera.hpp"
#include <future>
#include <memory>

struct GLFWwindow;
class TileMesh;
class Shader;
class Planet;

void showGameWindow(const LauncherConfig& cfg, LoadResult& loadResult);

// Internal: GameWindow class with separated update/render phases
class GameWindow {
public:
    bool init(const LauncherConfig& cfg, LoadResult& loadResult);
    void run();

private:
    GLFWwindow* m_window = nullptr;
    GLFWwindow* m_sharedWindow = nullptr;
    std::future<void> m_buildFuture;
    bool m_meshReady = false;
    bool m_useAsyncBuild = false;
    const LauncherConfig* m_cfg = nullptr;
    LevelData* m_level = nullptr;
    Timeline* m_timeline = nullptr;
    PlaybackClock* m_playback = nullptr;
    HitsoundManager* m_hitsoundMgr = nullptr;
    AudioEngine* m_audioEngine = nullptr;

    struct Input {
        bool dragActive = false;
        double dragStartX=0, dragStartY=0, cursorX=0, cursorY=0;
        double baseTargetX=0, baseTargetY=0, offsetX=0, offsetY=0;
        int selectedTile = -1;
        bool justClicked = false;
        Camera* camera = nullptr;
    };

    TileMesh* m_tileMesh = nullptr;
    Camera m_camera;
    std::unique_ptr<Planet> m_redPlanet;
    std::unique_ptr<Planet> m_bluePlanet;
    bool m_tileVisEnabled = false;
    int m_lastHiddenEnd = -1;
    bool m_sgVisibleLatch = false;
    Input m_input;
    float m_targetAspect = 16.0f/9.0f;
    float m_bgR=0, m_bgG=0, m_bgB=0;
    double m_lastFrameTime = 0;
    bool m_wasSpacePressed = false;
    bool m_wasAltEnterPressed = false;
    double m_autoPlayTriggerTime = 0;
    bool m_musicPending = false;
    bool m_exclusiveFullscreen = true;
    bool m_isFullscreen = false;
    int m_windowedX = 0, m_windowedY = 0;
    int m_windowedW = 1920, m_windowedH = 1080;

    // Rendering objects
    Shader* m_tileShader = nullptr;
    Shader* m_planetShader = nullptr;
    Shader* m_trailShader = nullptr;
    Shader* m_highlightShader = nullptr;

    void handleInput();
    void update(float deltaMs);
    void render();
    void applyPlaybackFrame();
    void toggleFullscreen();
};
