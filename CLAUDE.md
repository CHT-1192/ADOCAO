# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

A native C++ / OpenGL "A Dance of Fire and Ice" (冰与火之舞) level viewer. Plays `.adofai` custom levels on Windows and Linux (Wayland).

A Vulkan port exists at `../ADOCAV/` (same game logic, Vulkan 1.2 backend with GPU compute culling).

Reference implementations:
- `../ADOFAI-JS/` — Core angle parsing
- `../Re_ADOJAS/` — Three.js web player (hitsound, camera, decorations)
- `../ADOFAN_PIXI/` — PixiJS web player
- `../ADOFAI/A Dance of Fire and Ice/` — Original Unity game. Use dnSpy to decompile `Assembly-CSharp.dll`

A WinUI 3 C# launcher is in a separate repo at `../ADOCAO_WinUI3_Launcher/`.

## Branches

- `master` — Current development branch (OpenGL 4.3, compute shaders ready)
- `pre-synth-v1` — Same as master (merged)

## Floating-Point Precision Rules

**ALL timing and position values must use `double` (float64).** Float32 precision loss at extreme values causes artifacts.
- `angleData`: `std::vector<double>` (16 decimal places)
- `tileStartTimes[]`, `m_elapsedTime`, camera target: all `double`
- GPU uploads: `float` only at last step (camera-relative offset), values stay small

## Build & Run

**Windows:** `build.bat` (`build.bat portable` for static, `build.bat highfps` for 1000fps cap)
**Linux:** `chmod +x build.sh && ./build.sh`
**Manual:** `mkdir build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && cmake --build . --parallel`

Minimum CMake 3.20. C++20. OpenGL 4.3+ required. Dependencies via FetchContent (GLFW, glm, nlohmann/json, Dear ImGui, miniaudio, tinyfiledialogs).

`--debug` flag enables debug console, disables hitsounds by default.

## CLI Usage

```
adocao.exe --level <file> --music <file> [--width N] [--height N]
           [--fullscreen] [--fill HEX] [--stroke HEX] [--bg HEX]
           [--no-auto-stroke] [--no-hitsound] [--no-trail] [--debug]
           [--force-hitsound] [--auto-play] [--export]
```

Without `--level`, falls through to the ImGui launcher.

## Audio System

### Music
`ma_decoder` (miniaudio) — supports AIFF, OGG, WAV, FLAC. File read into memory + `ma_decoder_init_memory()`. Output: stereo f32 @ 44100Hz. Device period: 1024 frames (~23ms) for best quality. `m_fileData` kept alive for decoder lifetime. Pause stops the audio device (not just sets a flag).

### Hitsounds
Pre-synthesis with 16-bit hard-clip integer mixing (matches `HitSoundGenerator.exe`):
- Multi-type support via `TimestampGroup` (SetHitsound events), 27 hit types
- Case-insensitive type matching (ADOFAI levels may use mixed case)
- Unknown type fallback: redirects to default type if WAV not found
- `--force-hitsound`: override "None" type → "Kick" (GUI: "Force HS" checkbox)
- Per-group WAV loading with volume scaling, cached in `s_wavCache` + `s_wavRawCache`
- `preSynthesize()` → stereo float buffer → streamed via `attachExternal()`
- Export: launcher Export button or `--export` CLI writes `<level>_hitsounds.wav`

## Playback Engine

Direction-based angle algorithm matching ADOFAI-JS `_parseAngle`. BPM propagation via pre-indexed SetSpeed events (O(n+m)). Twirl toggles `isCW` before computing angle — affects current tile. Full rotation only when `delta < 0.0001`. Double precision throughout. `--auto-play` flag auto-starts playback 0.5s after loading.

## Coordinate System

OpenGL world space: X right, Y up. View matrix always at origin — camera-relative offsets computed in double, converted to float for GPU.

## Rendering

### OpenGL 4.3 Core Profile
Compute shader ready: GPU frustum culling (`kTileCullCompSrc`) and offset computation (`kTileOffsetCompSrc`) shaders available. SSBO + indirect draw functions loaded. Not yet wired into the draw loop (Phase 4).

### Per-instance color attributes
Vertex shader: `aType` (0=stroke, 1=fill) mixes `iColor`/`iBgColor` per-instance.
- Vertex VBO: `[x, y, z, type]` — 4 floats per vertex
- Instance pos VBO: `[offX, offY, offZ]` — 3 floats, uploaded per-frame
- Instance color VBO: `[fillR,fillG,fillB, strokeR,strokeG,strokeB, opacity]` — 7 floats, static

### Visibility cache
`draw()` caches visible instance indices per shape group. Rebuilt when frustum bounds change (position or zoom). Camera-relative offsets recomputed each frame on cached set. Uses `frustumChanged()` checking both position and viewport size.

### ColorTrack
Parsed via `processActions()` → per-tile `tileFillColors[]`/`tileStrokeColors[]`. Applied during `TileMesh::build()` as per-instance colors. ColorTrack events (basic parsing done, COLOR_FUNCS/Pulse/RecolorTrack runtime TBD).

### Memory management
`LevelData::releaseMemory()` frees angleData, actions/decorations JSON, tilePositionOffsets, tileFillColors, tileStrokeColors after loading. `tileBPMs` kept — needed by `buildIcons()` for SetSpeed icon coloring.

### Tiles drawn without depth test
Descending instance order for basic layering. Icons drawn last with depth test off (always on top).

### Frame pacing
Sleep-based: `sleep_for(remaining - 1ms)` + spin last 1ms for precision. 320 FPS soft cap (1000 with highfps build). DPI awareness + CPU pin to performance cores on Windows.

## Reference Implementations

- `../ADOFAI-JS/` — Core angle parsing
- `../Re_ADOJAS/` — Three.js web player (hitsound, camera, decorations)
- `../ADOFAN_PIXI/` — PixiJS web player
- `../ADOFAI/A Dance of Fire and Ice/` — Original Unity game. Use dnSpy to decompile `Assembly-CSharp.dll`
- `../ADOCAV/` — Vulkan 1.2 port (same game logic, GPU compute culling)
