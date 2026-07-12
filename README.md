# ADOCAO - A Dance of C++ and OpenGL

A native high-performance viewer for [A Dance of Fire and Ice](https://store.steampowered.com/app/977950/A_Dance_of_Fire_and_Ice/) custom levels (`.adofai` format), written in C++20 and OpenGL 4.3.

![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)
![OpenGL](https://img.shields.io/badge/OpenGL-4.3-red)
![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-lightgrey)

A [Vulkan 1.2 port](https://github.com/CHT-1192/ADOCAV) is also available.

## Features

- Full ADOFAI playback engine with relative angle computation (matches [ADOFAI-JS](https://github.com/adofaiex/ADOFAI-JS))
- Music playback via miniaudio (WASAPI/PulseAudio): AIFF, OGG, WAV, FLAC
- Pre-synthesized hitsound tracks (27 hit types) with 16-bit hard-clip mixing
- Instanced GPU rendering with frustum culling and visibility cache
- Per-instance fill and stroke colors via vertex attributes
- Planet movement with trail rendering (Catmull-Rom spline)
- Event icons: Twirl (purple), SetSpeed (red/blue)
- Z-depth rendering (far-to-near tile ordering, supports 7M-tile levels)
- Bookmark navigation (Ctrl+Left/Right) + track selection (click to play from tile)
- DPI-aware launcher with auto-stroke color, background color, and resolution selection
- OpenGL 4.3 compute shaders ready (GPU frustum culling)
- `SetSpeed`, `Twirl`, `Pause`, `Midspin` event support
- `--auto-play` for automatic playback
- `--force-hitsound` to override level hitsound type
- `--cpu-culling` to disable GPU culling experiments

## Quick Start

### Windows
```bash
build.bat                     # default: 320 FPS, zoom max 1000
build.bat highfps             # 1000 FPS cap
build.bat exzoom              # max zoom 4000
build.bat highfps exzoom      # both
build.bat portable            # static-linked portable build
build.bat portable highfps    # portable + 1000 FPS
```

### Linux
```bash
chmod +x build.sh
./build.sh                    # default: 320 FPS, zoom max 1000
./build.sh highfps            # 1000 FPS cap
./build.sh exzoom             # min zoom 1.0
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `ADOCAO_PORTABLE` | OFF | Static-link portable build |
| `ADOCAO_HIGH_FPS` | OFF | 1000 FPS cap (default 320) |
| `ADOCAO_EXTREME_ZOOM` | OFF | Farthest zoom-out 0.5 (default 5.0) |

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

## CLI Usage

```
adocao.exe --level <file> --music <file> [--width N] [--height N]
           [--fullscreen] [--fill HEX] [--stroke HEX] [--bg HEX]
           [--no-auto-stroke] [--no-hitsound] [--no-trail] [--debug]
           [--force-hitsound] [--auto-play] [--export] [--cpu-culling]
```

Without `--level`, falls through to the ImGui launcher.

## Project Structure

```
app/         Application layer (windows, launcher, game loop)
audio/       Music playback + hitsound synthesis (miniaudio)
camera/      Orthographic camera with frustum culling
game/        Planet rendering + playback engine
glad/        OpenGL 4.3 Core loader (custom minimal loader)
level/       .adofai parser + JSON cleaner
render/      Shader programs + planet trail rendering
shaders/     GLSL shader source files (.vert / .frag / .comp)
track/       Tile mesh generation + instanced rendering
util/        Logger + easing functions
hitsounds/   27 hit sound .wav files
```

## Controls

| Key | Action |
|-----|--------|
| Space | Start/stop playback |
| Esc | Close game window |
| Mouse drag | Pan camera (when stopped) |
| Scroll | Zoom in/out (5–1000) |

## Acknowledgements

This project draws heavily from:
- [Re_ADOJAS](https://github.com/adofaiex/Re_ADOJAS) — Three.js web player
- [ADOFAN_PIXI](https://github.com/AnStartist/ADOFAN_PIXI) — PixiJS web player

Thank you to their authors for the reference implementations.

## License

Apache 2.0
