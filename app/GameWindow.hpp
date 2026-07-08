#pragma once

#include "LauncherWindow.hpp"
#include "LevelLoader.hpp"
#include "camera/Camera.hpp"

struct GLFWwindow;
class TileMesh;
class Shader;

void showGameWindow(const LauncherConfig& cfg, LoadResult& loadResult);

// Internal: GameWindow class with separated update/render phases
class GameWindow {
public:
    bool init(const LauncherConfig& cfg, LoadResult& loadResult);
    void run();

private:
    GLFWwindow* m_window = nullptr;
    const LauncherConfig* m_cfg = nullptr;
    LevelData* m_level = nullptr;
    PlaybackEngine* m_playback = nullptr;
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
    Input m_input;
    float m_targetAspect = 16.0f/9.0f;
    float m_bgR=0, m_bgG=0, m_bgB=0;
    double m_lastFrameTime = 0;
    bool m_wasSpacePressed = false;
    double m_autoPlayTriggerTime = 0;

    // Rendering objects
    Shader* m_tileShader = nullptr;
    Shader* m_planetShader = nullptr;
    Shader* m_trailShader = nullptr;
    Shader* m_highlightShader = nullptr;

    void handleInput();
    void update(float deltaMs);
    void render();
};
