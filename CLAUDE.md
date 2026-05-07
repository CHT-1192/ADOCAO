# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

A native C++ / OpenGL "A Dance of Fire and Ice" (冰与火之舞) level viewer. Plays `.adofai` custom levels on Windows and Linux (Wayland). The reference implementation is the TypeScript/Three.js web player at `../re_adojas/` — all game logic, timing, and level parsing should match that implementation.

## Build & Run

**Windows (MinGW):**
```bash
build.bat
```

**Linux (Wayland):**
```bash
sudo apt install libwayland-dev libxkbcommon-dev libgl-dev fonts-noto-cjk zenity
chmod +x build.sh
./build.sh
```

Or manually:
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel
./adocao
```

No command-line arguments. All configuration (level file, music, resolution, fullscreen, trail toggle) is selected in the launcher window.

Minimum CMake version 3.20. Uses C++20.

## Dependencies

| Library | Purpose |
|---------|---------|
| GLFW 3.4 | Window creation, input, OpenGL context |
| glad | OpenGL 3.3+ Core profile loader |
| glm | Vector/matrix math |
| nlohmann/json | Parse `.adofai` JSON level files |
| stb_image | Texture loading (unused currently) |
| miniaudio | Audio playback (music + hitsounds) |
| Dear ImGui | Launcher + loading window UI |
| tinyfiledialogs | Native OS file open dialogs |

## Feature Scope

**Implemented:**
- Track tile rendering (instanced, frustum-culled, colored fill/stroke)
- Event icons on tiles: Twirl (purple), SetSpeed-up (red), SetSpeed-down (blue)
- Planet movement (red & blue spheres, pivot alternation)
- Planet trail (Catmull-Rom ribbon, 0.4s duration, tapered width)
- Space key to start/stop playback
- **Audio playback** — music (.ogg/.mp3/.wav) + pre-synthesized hitsounds
- `SetSpeed` — BPM changes (multiplier or absolute)
- `Twirl` — flips CW/CCW rotation direction
- `Pause` — adds extra rotation (duration/2 full turns) to current tile
- `PositionTrack` — **only positionOffset with justThisTile** (cumulative offsets)
- Camera: orbit follows current pivot planet during playback, free drag/zoom when stopped
- High-DPI window scaling (per-monitor awareness, ImGui font scaling)
- Launcher: level/music browse, resolution, fullscreen, track colors, trail toggle

**Not yet implemented:**
- `MoveCamera`, `MoveTrack`, `ColorTrack`, `RecolorTrack`, `Bloom`, `Flash`
- `AddDecoration` / `MoveDecorations`
- PositionTrack: relativeTo, rotation, scale, opacity, stickToFloors

## File Structure

```
src/
  app/
    main.cpp               — Entry point (--debug flag)
    Application.h/.cpp     — 3-stage window flow + DPI awareness init
    LauncherWindow.h/.cpp  — ImGui launcher (file browse, reso, fullscreen, trail, colors)
    LoadingWindow.h/.cpp   — ImGui loading progress bar
    LevelLoader.h/.cpp     — Async loading pipeline
    GameWindow.h/.cpp      — OpenGL game window (rendering + playback + audio start/stop)
  audio/
    AudioEngine.h/.cpp     — Music playback via miniaudio (load, play, pause, seek, volume)
    HitsoundManager.h/.cpp — Hitsound synthesis (pre-mixed buffer from timestamps) + playback
  camera/
    Camera.h/.cpp          — Orthographic camera
  game/
    Planet.h/.cpp          — Planet sphere mesh (radius 0.25), GPU resources, trail link
    PlaybackEngine.h/.cpp  — Timing precalculation, planet movement, pivot logic, hitsound timestamps
  glad/
    gl_core.h/.cpp         — OpenGL core profile loader
  level/
    LevelData.h/.cpp       — ADOFAI JSON parser, tile position calc, action processing
    JsonCleaner.h/.cpp     — JSON fixup (missing commas, trailing commas) + parseBool helper
  render/
    Shader.h/.cpp          — OpenGL shader compilation + uniform setters
    Shaders.h              — All embedded GLSL source strings (tile, planet, trail)
    PlanetTrail.h/.cpp     — Catmull-Rom ribbon trail (200 max pts, 0.4s, semi-transparent)
  track/
    TileMesh.h/.cpp        — Instanced tile + icon rendering, frustum culling, GPU upload
    TileGeometry.h/.cpp    — Geometry generators (createCircle, createTileMesh, createMidSpinMesh)
  util/
    Easing.h               — Standard ADOFAI easing functions
    Logger.h/.cpp          — File + console logger
    stb_impl.cpp           — stb_image implementation unit
assets/
  sounds/                  — 27 .wav hit sound files (copied from re_adojas)
```

## Playback Engine

### Timing Model

Precalculated in `PlaybackEngine::precalculateTiming()` (matches `Player.ts::calculateCumulativeRotations()`):

- `secPerBeat = 60 / currentBPM` (BPM starts from `settings.bpm`, changes via SetSpeed events)
- For tile `i`: `rawAngle = angleData[i]` (999 for midspin), `totalAngle = rawAngle * PI / 180`
- CW flip: if Twirl toggles isCW, `totalAngle = -totalAngle`
- Pause: `extraRotation += event.duration / 2.0` (in full 360° units)
- `rotationAmount = abs(totalAngle) / (2*PI)` (number of full rotations)
- `tileDurations[i] = rotationAmount * 2 * (60 / currentBPM)` (seconds to traverse tile i)
- `tileStartTimes[i]` = cumulative time, shifted so `tileStartTimes[1] = 0` (planet hits tile 1 at t=0)
- Countdown: `countdownTicks * (60 / settings.bpm)` seconds before tile 1
- `timeInLevel = elapsedSec - countdownDuration`

### Planet Movement

- Pivot tile index alternates: even index → red pivot, odd index → blue pivot
- Pivot planet sits at tile position, moving planet orbits around it
- Normal: `progress = (timeInLevel - tileStartTimes[i]) / tileDurations[i]`
  `currentAngle = tileStartAngles[i] + tileTotalAngles[i] * progress`
  `movingPos = pivotPos + (cos(angle)*dist, sin(angle)*dist)`
- Past last tile: infinite rotation at `(BPM/60)*PI` rad/s
- Planet Z = 1.0 (above tiles), trail Z = 0

### Input

- **Space**: start/stop playback (edge-triggered, one press = toggle)
- **Esc**: close game window
- **Mouse drag**: pan camera (only when not playing)
- **Mouse scroll**: zoom (5–500 range)

## Audio System

### Music (AudioEngine)

Wraps miniaudio `ma_engine` + `ma_sound` for OGG/MP3/WAV music playback.
- `init()` creates the audio engine; `loadMusic(path)` loads a file
- `play()` / `playScheduled(delaySeconds)` / `pause()` / `stop()` / `seek(seconds)`
- `position()` returns cursor in seconds; `volume` is 0.0-1.0
- On space press: music is seeked to `offset/1000`, then started with a scheduled delay:
  - `musicStartDelay = countdownDuration - musicDelaySeconds`
  - `musicDelaySeconds = (firstTileAngle - 180) / 180 * secPerBeat`
  - If delay negative, music starts immediately at the calculated offset

### Hitsounds (HitsoundManager)

Pre-synthesizes all hits into a single WAV buffer (matches reference `HitsoundManager.ts`):
- `preSynthesize(timestamps, totalDuration)` — mixes the selected hitsound .wav at each timestamp
- Soft-clip normalization (polynomial tanh approximation, threshold 0.5)
- Writes temp WAV via manual RIFF header + int16 PCM
- `start(delaySeconds)` — plays the synthesized track (delayed start for countdown sync)
- `stop()` / `dispose()` — stops playback, deletes temp WAV

**Hitsound timestamps**: `PlaybackEngine::getHitsoundTimestamps()` collects `tileStartTimes[i]` for i ≥ 1 (skipping tiles with raw angleData == 0). Timestamps represent when planet reaches each tile relative to tile 1.

**Type mapping** (matching reference `hitsoundKeyMap`):
- `Kick` → sndKick.wav, `Snare` → sndSnareAcoustic2.wav, `Hat` → sndHat.wav, etc.
- 27 hitsound types supported (all .wav files in `assets/sounds/`)

### Audio Sync on Space Press

```
countdownDuration = countdownTicks * (60 / initialBPM)
musicDelaySeconds = (firstTileAngle - 180) / 180 * secPerBeat
musicStartDelay = countdownDuration - musicDelaySeconds
hitsoundStartDelay = countdownDuration

music.seek(offset / 1000)
if musicStartDelay > 0: music.playScheduled(musicStartDelay)
else: music.seek(offset/1000 + |musicStartDelay|); music.play()
hitsounds.start(hitsoundStartDelay)
```

## Event Icons

Painted on track tiles during `TileMesh::buildIcons()`. Match re_adojas visual:
- Icon circle radius: 0.18 world units (16 segments)
- Z offset: +0.002 above tile (+0.001 extra for SetSpeed when Twirl also present)
- Colors: Twirl = purple (0x800080), SpeedUp = red (0xFF0000), SpeedDown = blue (0x0000FF)
- SetSpeed icon only shown when BPM ratio > 1.05 (up) or < 0.95 (down)

## Midspin Detection

Midspin = `angleData[i] == 999.0f` (matches reference `angleData[index] === 999`).
The shape key in `TileMesh::build()` uses raw angleData, not a heuristic.

## Coordinate System

OpenGL world space: X right, Y up, Z toward camera (orthographic). Tile Z = (12 - index) * 0.1. Camera looks along -Z.

## High DPI

Windows: `SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE)` called in `Application.cpp`.
Launcher + loading windows scale by `glfwGetMonitorContentScale()`. ImGui `FontGlobalScale` set to DPI scale.
Game window uses user-requested resolution directly.

## Key ADOFAI Level Format

JSON with these top-level keys:
- `angleData: number[]` — Angle per tile (999 = midspin, 180 = straight)
- `settings: { bpm, offset, countdownTicks, zoom, rotation, ... }`
- `actions: [{ floor, eventType, ... }]` — Supported: SetSpeed, Twirl, Pause, PositionTrack (positionOffset + justThisTile only)
- `pathData: string` — Alternative to angleData (R=0°, L=180°, !=midspin, etc.)
- `decorations: [...]` — Ignored
