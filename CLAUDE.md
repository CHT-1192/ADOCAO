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

**Portable (static linked):**
```bash
build.bat portable
```

`--debug` flag enables debug console, disables hitsounds by default.

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
- Track tile rendering (instanced, frustum-culled with visibility cache, double-precision culling)
- Event icons on tiles: Twirl (purple), SetSpeed-up (red), SetSpeed-down (blue), drawn last (always on top)
- Planet movement (red & blue spheres, pivot alternation, Z=3.0 above tiles)
- Planet trail (Catmull-Rom ribbon, 0.4s duration, center-relative VBO for precision)
- Space key to start/stop playback (wall-clock driven, surviving pauses/drags)
- **Audio playback** — music (.ogg via stb_vorbis) + pre-synthesized hitsounds (per-sample softClip)
- `SetSpeed` — BPM changes (multiplier or absolute), affects current tile
- `Twirl` — flips CW/CCW rotation direction, affects current tile
- `Pause` — adds extra rotation (duration/2 full turns) to current tile
- `PositionTrack` — **only positionOffset with justThisTile** (cumulative offsets)
- Camera: orbit follows current pivot during playback, free drag/zoom when stopped
- View-at-origin + camera-relative rendering (eliminates GPU float32 precision loss at extreme distances)
- Tile positions and timing stored as double throughout pipeline
- High-DPI window scaling (per-monitor awareness, native-res font loading, widget size scaling)
- Launcher: level/music browse, resolution, fullscreen, track colors, background color, trail, auto-stroke, hitsound toggle
- `--debug` flag: disables hitsounds by default, enables debug console log
- Portable static build: `build.bat portable` → ADOCAO-Portable.exe
- Memory-mapped file reading on Windows, geometry/WAV caches

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

`angleData` is treated as **absolute directions** (0=right, 90=up, 180=left, 270=down). Relative rotation per tile is computed via ADOFAI-JS `_parseAngle` algorithm from direction deltas.

- `secPerBeat = 60 / currentBPM` (BPM starts from `settings.bpm`, changes via SetSpeed events)
- For tile `i`: `delta = normalize(angleDir - angleData[i])` — direction change in degrees
- `relAngle = delta` (or `360-delta` if twirled, or `360` if delta==0)
- `totalAngle = relAngle * PI / 180`; if `isCW`: `totalAngle = -totalAngle`
- Next tile entry: `angleDir = normalize(angleData[i] + 180)`
- Midspin (`angleData[i]==999`): `relAngle = 0`, `angleDir = normalize(angleData[i-1])`
- Pause: `extraRotation += event.duration / 2.0` (in full 360° units)
- `rotationAmount = abs(totalAngle) / (2*PI)`; `duration = rotationAmount * 2 * secPerBeat`
- `tileStartTimes[]` (double) = cumulative, shifted so `tileStartTimes[1] = 0`
- Countdown: `countdownTicks * (60 / settings.bpm)` seconds before tile 1 (always uses initial BPM)
- `timeInLevel = elapsedMs / 1000 - countdownDuration`

### Twirl / SetSpeed

- Events pre-indexed by floor (O(1) lookup per tile)
- Twirl toggles `isCW` before computing tile's angle — affects **current** tile
- SetSpeed changes `currentBPM` before computing tile's angle — affects **current** tile
- BPM propagation: pre-indexed per floor (O(n+m), not O(n*m))

### Planet Movement

- Pivot tile index alternates: even index → red pivot, odd index → blue pivot
- `progress = (timeInLevel - tileStartTimes[i]) / tileDurations[i]`, clamped [0,1]
- `currentAngle = startAngle + totalAngle * progress`
- `movingPos = pivotPos + (cos(angle)*dist, sin(angle)*dist)`
- Past last tile: infinite rotation at `(BPM/60)*PI` rad/s
- Planet Z = 3.0 (above tiles), trail Z = 0 (drawn before planets, no depth test)

### Timing Clock
- **Music loaded**: `syncToAudio(audioPos, offset)` — visual follows audio clock exactly
- **No music**: `updateWallClock(glfwGetTime())` — absolute wall clock, survives pauses/drags

### Input

- **Space**: start/stop playback (edge-triggered)
- **Esc**: close game window
- **Mouse drag**: pan camera (only when not playing)
- **Mouse scroll**: zoom (5–500 range)

## Audio System

### Music (AudioEngine)

Uses `ma_device` (WASAPI/PulseAudio) + `stb_vorbis` for OGG decoding. File is memory-mapped via `_wfopen` on Windows for UTF-8 path support.
- `init()` creates playback device; `loadMusic(path)` opens OGG via stb_vorbis
- `play()` / `pause()` / `stop()` / `seek(seconds)` / `resume()`
- `position()` returns decoder cursor in seconds
- On space press: `seek(offset/1000)` then `play()` — audio starts from offset immediately
- Music + hitsounds mixed in single `dataCallback` (prevents clock drift)
- External source support via `attachExternal(buffer, cursor, playing)`

### Hitsounds (HitsoundManager)

Pre-synthesizes all hits into an in-memory float buffer (stereo, 44100Hz):
- `preSynthesize(timestamps, totalDuration)` — multi-threaded mixing with per-sample softClip
- Polynomial softClip (matches reference): transparent for |x|<0.5, gentle for 0.5–1.5, hard clip at ±1
- No global gain normalization — quiet sections unaffected by dense overlaps
- In-memory buffer streamed via AudioEngine's mixer callback (no temp WAV file)
- WAV decode cache: decoded samples reused across level loads
- 27 hitsound types mapped via `hitsoundKey()` (matching reference `hitsoundKeyMap`)

**Hitsound timestamps**: `PlaybackEngine::getHitsoundTimestamps()` collects `tileStartTimes[i]` (double) + countdown offset for i ≥ 1.

## Event Icons

Painted during `TileMesh::buildIcons()`. Match re_adojas visual:
- Icon circle radius: 0.11 (matches 0.275 × 0.8 / 2)
- Z offset: +0.01 above tile (+0.005 extra for SetSpeed when Twirl also present)
- Colors: Twirl = purple (0x800080), SpeedUp = red (0xFF0000), SpeedDown = blue (0x0000FF)
- SetSpeed icon only shown when BPM ratio > 1.05 (up) or < 0.95 (down)

## Midspin Detection

Midspin = `angleData[i] == 999.0f` (matches reference `angleData[index] === 999`).

## Coordinate System

OpenGL world space: X right, Y up. Camera at Z=10 looking at Z=0 (orthographic, near=0.1 far=50000). View matrix always at origin — all camera-relative offsets computed in double, converted to float for GPU. Tile Z = 2.0 - index*0.001.

## High DPI

Windows: `SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE)` in `Application.cpp`. Launcher/loading windows: `ImGui::GetStyle().ScaleAllSizes(dpiScale)`, fonts loaded at `16*dpiScale` native resolution, widget sizes multiplied by `dpiScale`. Game window uses user-requested resolution directly.

## Key ADOFAI Level Format

JSON with these top-level keys:
- `angleData: number[]` — Absolute direction per tile (999 = midspin). Relative rotation computed from direction deltas
- `settings: { bpm, offset, countdownTicks, zoom, rotation, ... }`
- `actions: [{ floor, eventType, ... }]` — Supported: SetSpeed, Twirl, Pause, PositionTrack (positionOffset + justThisTile only)
- `pathData: string` — Alternative to angleData (R=0°, L=180°, !=midspin, etc.)
- `decorations: [...]` — Ignored
