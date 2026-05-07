# ADOCAO - A Dance of Fire and Ice Level Viewer

A native C++/OpenGL viewer for [A Dance of Fire and Ice](https://store.steampowered.com/app/977950/A_Dance_of_Fire_and_Ice/) custom levels (`.adofai` format).

![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)
![OpenGL](https://img.shields.io/badge/OpenGL-3.3-green)
![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-lightgrey)

## Features

- Full ADOFAI playback engine with relative angle computation (matches [ADOFAI-JS](https://github.com/ADOFAI/ADOFAI-JS))
- Music playback (.ogg) via stb_vorbis + miniaudio (WASAPI/PulseAudio)
- Pre-synthesized hitsound tracks (27 hit types)
- Instanced GPU rendering with frustum culling and visibility cache
- DPI-aware launcher with auto-stroke color, background color, and resolution selection
- Planet movement with trail rendering (Catmull-Rom spline)
- `SetSpeed`, `Twirl`, `Pause`, `Midspin`, `PositionTrack` support

## Quick Start

### Windows
```bash
build.bat          # dynamic-link build
build.bat portable # static-link portable build
```

### Linux
```bash
chmod +x build.sh
./build.sh
```

## Build Dependencies

All dependencies are fetched automatically via CMake `FetchContent`:

| Library | Version | Purpose |
|---------|---------|---------|
| GLFW | 3.4 | Window + input |
| glm | 1.0.1 | Math |
| nlohmann/json | 3.11.3 | .adofai parsing |
| Dear ImGui | 1.91.9 | Launcher UI |
| miniaudio | 0.11.22 | Audio playback |
| stb_vorbis | latest | OGG decoding |
| tinyfiledialogs | 2.9.3 | File open dialogs |

## Project Structure

```
src/
  app/         Application layer (windows, launcher, game loop)
  audio/       Music playback + hitsound synthesis
  camera/      Orthographic camera with orbit controls
  game/        Planet rendering + playback engine
  glad/        OpenGL 3.3 Core loader
  level/       .adofai parser + JSON cleaner
  render/      Shader programs + planet trail
  track/       Tile mesh generation + instanced rendering
  util/        Logger + easing functions
assets/
  sounds/      27 hit sound .wav files
```

## Controls

| Key | Action |
|-----|--------|
| Space | Start/stop playback |
| Esc | Close game window |
| Mouse drag | Pan camera (when stopped) |
| Scroll | Zoom in/out |

## License

MIT
