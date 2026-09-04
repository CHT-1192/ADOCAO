# AGENTS.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

A native C++ / OpenGL "A Dance of Fire and Ice" (冰与火之舞) level viewer. Plays `.adofai` custom levels on Windows, Linux (Wayland) and macOS.

A Vulkan port exists at `../ADOCAV/` (same game logic, Vulkan 1.2 backend).

Reference implementations:
- `../ADOFAI-JS/` — Core angle parsing
- `../Re_ADOJAS/` — Three.js web player (hitsound, camera, decorations)
- `../ADOFAN_PIXI/` — PixiJS web player
- `../ADOFAI/A Dance of Fire and Ice/` — Original Unity game. Decompile `Assembly-CSharp.dll` with:
  `../ADOFAI/dnSpy/dnSpy.Console.exe -o ../ADOFAI/decomp_game "../ADOFAI/A Dance of Fire and Ice/A Dance of Fire and Ice_Data/Managed/Assembly-CSharp.dll"`
- `../ADOFAI_HitSound/` — Maicy0609's C++ hitsound generator (RapidJSON, Nyquist filter, equal-power pre-scaling, double mixing). Reference for loading performance and hitsound synthesis.

A WinUI 3 C# launcher is in a separate repo at `../ADOCAO_WinUI3_Launcher/`.

## Branches

- `master` — Current development branch (OpenGL 4.3)

## Floating-Point Precision Rules

**ALL timing and position values must use `double` (float64).** Float32 precision loss at extreme values causes artifacts.
- `angleData`: `std::vector<double>` (16 decimal places)
- `tileStartTimes[]`, `m_elapsedTime`, camera target: all `double`
- GPU uploads: `float` only at last step (camera-relative offset), values stay small

## Build & Run

**Windows:** `build.bat` (`build.bat portable` for static, `build.bat exzoom` for min zoom 0.5)
**Linux / macOS:** `chmod +x build.sh && ./build.sh` — 首次运行提问；已有 `build/` 缓存后不带参数运行即增量构建（不提问、不重新 configure；改选项需带参数如 `-U -Portable`）
**Manual:** `mkdir build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && cmake --build . --parallel`

Minimum CMake 3.20. C++20. OpenGL 4.3+ required. Dependencies via FetchContent (GLFW, glm, RapidJSON, Dear ImGui, miniaudio, stb, miniz, tinyfiledialogs).

`--debug` flag enables debug console, disables hitsounds by default.

## CLI Usage

```
adocao.exe --level <file> --music <file> [--width N] [--height N]
           [--fullscreen] [--fill HEX] [--stroke HEX] [--bg HEX]
           [--no-auto-stroke] [--no-hitsound] [--no-trail] [--debug]
           [--force-hitsound [TYPE]] [--auto-play] [--export] [--legacy-culling]
           [--msaa N] [--exclusive | --no-exclusive]
           [--trail-duration SEC] [--trail-sample-rate N]
```

Without `--level`, falls through to the ImGui launcher.

## Audio System

### Music
`ma_decoder` (miniaudio) — supports AIFF, OGG, WAV, FLAC. File read into memory + `ma_decoder_init_memory()`. Output: stereo f32 @ 48000Hz. Device period: 1024 frames (~23ms) for best quality. `m_fileData` kept alive for decoder lifetime. Pause stops the audio device (not just sets a flag).

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

Direction-based angle algorithm matching ADOFAI-JS `_parseAngle`. BPM propagation via pre-indexed SetSpeed events (O(n+m)). Twirl toggles `isCW` before computing angle — affects current tile. Full rotation only when `delta < 0.0001`. Double precision throughout.

`precalculateTiming()` split into 4 phases: (0) actions-by-floor index, (1) sequential state propagation (isCW/BPM/angleDir/extraRot), (2) parallel tile geometry via `std::async` (>= 256 tiles threshold), (3) sequential prefix sum → `tileStartTimes`, (4) last-tile handling.

`startAt(wallClockSec, audioPosSec, offsetSec)` supports mid-playback start from any tile. `findTileIndex()` binary-searches `m_tileStartTimes`. Bookmark navigation (Ctrl+←/→, stopped only) uses `jumpToTile()`. Track selection: click tile while stopped, Space starts from selected tile.

`--auto-play` auto-starts playback 0.5s after loading.

## Coordinate System

OpenGL world space: X right, Y up. View matrix always at origin — camera-relative offsets computed in double, converted to float for GPU.

## Rendering

### Shader files
GLSL source in `shaders/` directory — loaded from files at runtime via `Shader::compileFile()`. Embedded fallback strings remain in `render/Shaders.hpp`.

### Z-depth render order
Tiles and icons use depth test ON with per-instance Z values encoding far-to-near order. Ortho far plane reduced to 200 for depth precision (~755K steps, supports 7M-tile levels). Z allocation:
- Tile fill: `tileZ(i, n)` (0.0 far → 9.0 near), vertex Z +0.001
- Tile stroke: `tileZ(i, n)`, vertex Z 0.0
- Icons: `tileZ + 0.002` (twirl) / `+0.003~0.005` (SetSpeed)
- Planets: Z = 9.5 (always in front)
- Trails: depth test OFF, always visible
- Highlight: depth test OFF, always visible

### Per-instance color attributes
Vertex shader: `aType` (0=stroke, 1=fill) mixes `iColor`/`iBgColor` per-instance.
- Vertex VBO: `[x, y, z, type]` — 4 floats per vertex
- Instance pos VBO: `[offX, offY, offZ]` — 3 floats, uploaded per-frame
- Instance color VBO: `[fillR,fillG,fillB, strokeR,strokeG,strokeB, opacity]` — 7 floats, static

### Visibility cache
`draw()` caches visible instance indices per shape group. Rebuilt when frustum bounds change (position or zoom). Camera-relative offsets recomputed each frame on cached set. Multithreaded CPU culling via `std::async` for >= 64 groups.

### Memory management
`LevelData::releaseMemory()` frees angleData, actions/decorations JSON, tilePositionOffsets after loading. `tileBPMs` kept — needed by `buildIcons()` for SetSpeed icon coloring.

### Frame pacing
Sleep-based: `sleep_for(remaining - 1ms)` + spin last 1ms for precision. 320 FPS soft cap. DPI awareness + CPU pin to performance cores on Windows.

### Event icons
Twirl (purple), SetSpeed up (red), SetSpeed down (blue). Per-tile icon instances with depth-sorted Z.

### Highlight
Selected tile drawn with inverted colors via dedicated highlight shader (`1.0 - vColor` in fragment shader). Same vertex layout as tile shader, reads per-instance colors.

## File extensions

All project headers use `.hpp`. Third-party includes (miniaudio, imgui, tinyfiledialogs) retain their original extensions.
