# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

A native C++ / OpenGL "A Dance of Fire and Ice" (冰与火之舞) level viewer. Plays `.adofai` custom levels on Windows and Linux (Wayland). The reference implementation is the TypeScript/Three.js web player at `../re_adojas/` — all game logic, timing, and level parsing should match that implementation.

## Build & Run

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel
./adocao
```

No command-line arguments. All configuration (level file, music, resolution, fullscreen) is selected in the launcher window.

Minimum CMake version 3.20. Uses C++20.

## Dependencies (planned)

| Library | Purpose |
|---------|---------|
| GLFW 3.x | Window creation, input, OpenGL context (supports Wayland via `GLFW_PLATFORM_WAYLAND`) |
| glad | OpenGL 3.3+ Core profile loader |
| glm | Vector/matrix math (replaces Three.js) |
| nlohmann/json | Parse `.adofai` JSON level files |
| stb_image | Texture loading for decorations/backgrounds |
| miniaudio (single-header) | Audio playback (music + hitsounds) |
| Dear ImGui | **Required.** Launcher window and loading progress window UI |
| tinyfiledialogs | **Required.** Native OS file open dialogs for level/music selection |
| libnyquist (optional) | OGG/MP3 decoding support for music |

## Simplified Feature Scope

This is a **level viewer**, not a full game engine. Supported vs unsupported features:

**Supported:**
- `SetSpeed` — BPM changes (multiplier or absolute), affects planet speed
- `Twirl` — flips CW/CCW rotation direction
- `Pause` — adds extra rotation (duration/2 degrees) to the current tile
- `PositionTrack` — **only positionOffset** (rotation, scale, opacity, relativeTo, justThisTile, stickToFloors are ignored)

**Ignored:**
- `MoveCamera` — camera is fixed: relativeTo=Player, position=(0,0), follows current pivot planet
- `MoveTrack`, `ColorTrack`, `RecolorTrack`, `Bloom`, `Flash` — visual effects not implemented
- `AddDecoration` / `MoveDecorations` — decorations ignored

## Architecture

### Layer 0: Level Parser (`src/level/`)
- `LevelData.h/.cpp` — Parses `.adofai` JSON. Stores `settings`, `angleData[]`, `tiles[]` (with computed positions). Processes SetSpeed, Twirl, Pause events for tile timing. Parses PositionTrack.positionOffset. All other event types are ignored.

### Layer 1: Track Geometry (`src/track/`)
- `TileMesh.h/.cpp` — Generates tile quad meshes from angleData positions.
- `TrackManager.h/.cpp` — Visibility culling, instanced rendering of visible tiles.

### Layer 2: Planet Movement (`src/game/`)
- `Planet.h/.cpp` — Single planet sphere. Two instances (red & blue).

**Core movement algorithm** (must match reference):
- BPM starts from `settings.bpm`, changes per tile via SetSpeed events
- `secPerBeat = 60 / currentBPM`
- For tile `i` (the pivot), `startAngle` = vector from pivot to previous tile
- `totalAngle = (abs(tile.angle) + extraRotation) * π / 180`
- At elapsed time `t`, `progress = (t - tileStartTimes[i]) / duration_i`, `currentAngle = startAngle + totalAngle * progress`
- Moving planet position: `pivotPos + (cos(currentAngle), sin(currentAngle)) * dist`
- Direction flips on Twirl events
- Pivot alternates: tile 0 → red is pivot, tile 1 → blue is pivot, etc.
- After the last tile: infinite rotation at constant BPM.

### Layer 3: Camera (`src/camera/`)
- `Camera.h/.cpp` — Orthographic camera, always follows the current pivot planet. Fixed relativeTo=Player, position=(0,0), zoom from settings.

### Layer 4: Rendering (`src/render/`)
- `Renderer.h/.cpp` — OpenGL state setup, viewport, render loop.
- `Shader.h/.cpp` — GLSL shader compilation and uniform management.
- `PlanetTrail.h/.cpp` — Ribbon/line trail behind moving planet.

### Layer 5: Audio (`src/audio/`)
- `AudioEngine.h/.cpp` — miniaudio music playback with seek.
- `HitsoundManager.h/.cpp` — Pre-synthesized hitsound buffer.

### Layer 6: Application (`src/app/`)
- `main.cpp` — Entry point.
- `Application.h/.cpp` — Three-window sequence: launcher → loading → game.
- `LauncherWindow.h/.cpp` — ImGui launcher. File selectors, resolution, fullscreen, Start.
- `LoadingWindow.h/.cpp` — ImGui progress bar + status text.
- `LevelLoader.h/.cpp` — Async multi-step loader with progress reporting.
- `GameWindow.h/.cpp` — Pure OpenGL game window. No ImGui. Space=pause, Esc=quit.

### Shared / Utilities (`src/util/`)
- `Easing.h` — All standard easing functions. Constants must match `Easing.ts` exactly.

## Key ADOFAI Level Format (.adofai)

JSON with these top-level keys:
- `angleData: number[]` — Angle for each tile (999 = midspin, treated as previous+180°). 180° = straight line.
- `settings: { bpm, offset, countdownTicks, hitsound, hitsoundVolume, trackColor, backgroundColor, zoom, ... }`
- `actions: [{ floor, eventType, ... }]` — Supported: `SetSpeed`, `Twirl`, `Pause`, `PositionTrack` (positionOffset only). Ignored: MoveCamera, MoveTrack, ColorTrack, RecolorTrack, Bloom, Flash, AddDecoration.
- `decorations: [...]` — **Ignored.**

## Timing Model

- `secPerBeat = 60 / currentBPM` (BPM starts from `settings.bpm`, changes via SetSpeed events)
- Countdown: `countdownTicks * secPerBeat` at initial BPM
- Music offset: `settings.offset` (ms), applied as `music.currentTime = offset/1000` at start
- `tileStartTimes[i]` = cumulative time from tile 0 to tile i. For tile i: `abs(angle_i + extraRotation) / 180 * secPerBeat_i`
- Planet hits tile i exactly when `timeInLevel == tileStartTimes[i]`

## Coordinate System

OpenGL world space: X right, Y up, Z toward camera (orthographic). Tile indices increase away from the camera (Z = (12 - index) * 0.1). Camera looks along -Z.
