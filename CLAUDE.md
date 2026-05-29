# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

A native C++ / OpenGL "A Dance of Fire and Ice" (冰与火之舞) level viewer. Plays `.adofai` custom levels on Windows and Linux (Wayland). Reference implementations at `../re_adojas/` (Three.js), `../ADOFAN_PIXI/` (PixiJS), and `../ADOFAI-JS/` (core angle parsing).

A WinUI 3 C# launcher is in a separate repo at `../ADOCAO_WinUI3_Launcher/`. It calls ADOCAO via CLI arguments (`--level`, `--music`, `--width`, etc.).

## Branches

- `master` — Independent AudioSource hit model (like Unity PlayScheduled)
- `pre-synth-v1` — First-gen pre-synthesis with 16-bit hard-clip (matches original HitSoundGenerator.exe), per-instance colors, ColorTrack parsing, visibility cache

## Floating-Point Precision Rules

**ALL timing and position values must use `double` (float64).** Float32 precision loss at extreme values causes artifacts.
- `angleData`: `std::vector<double>` (16 decimal places)
- `tileStartTimes[]`, `m_elapsedTime`, camera target: all `double`
- GPU uploads: `float` only at last step (camera-relative offset), values stay small

## Build & Run

**Windows:** `build.bat` (`build.bat portable` for static, `build.bat highfps` for 1000fps cap)
**Linux:** `chmod +x build.sh && ./build.sh`
**Manual:** `mkdir build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && cmake --build . --parallel`

Minimum CMake 3.20. C++20. Dependencies via FetchContent (GLFW, glm, nlohmann/json, Dear ImGui, miniaudio, tinyfiledialogs).

`--debug` flag enables debug console, disables hitsounds by default.

## CLI Usage

```
adocao.exe --level <file> --music <file> [--width N] [--height N]
           [--fullscreen] [--fill HEX] [--stroke HEX] [--bg HEX]
           [--no-auto-stroke] [--no-hitsound] [--no-trail] [--debug]
```

Without `--level`, falls through to the ImGui launcher.

## Audio System

### Music
`ma_decoder` (miniaudio) — supports AIFF, OGG, WAV, FLAC. File read into memory + `ma_decoder_init_memory()`. Output: stereo f32 @ 44100Hz. Device period: 1024 frames (~23ms) for best quality. `m_fileData` kept alive for decoder lifetime.

### Hitsounds (pre-synth-v1)
Pre-synthesis with 16-bit hard-clip integer mixing (matches `HitSoundGenerator.exe`):
- Multi-type support via `TimestampGroup` (SetHitsound events)
- Per-group WAV loading with volume scaling
- `preSynthesize()` → stereo float buffer → streamed via `attachExternal()`
- Export: launcher Export button writes `<level>_hitsounds.wav`

## Playback Engine

Direction-based angle algorithm matching ADOFAI-JS `_parseAngle`. BPM propagation via pre-indexed SetSpeed events (O(n+m)). Twirl toggles `isCW` before computing angle — affects current tile. Full rotation only when `delta < 0.0001` (not 0.01°). Double precision throughout.

## Coordinate System

OpenGL world space: X right, Y up. View matrix always at origin — camera-relative offsets computed in double, converted to float for GPU.

## Rendering (pre-synth-v1)

### Per-instance color attributes
Vertex shader: `aType` (0=stroke, 1=fill) mixes `iColor`/`iBgColor` per-instance.
- Vertex VBO: `[x, y, z, type]` — 4 floats per vertex
- Instance pos VBO: `[offX, offY, offZ]` — 3 floats, uploaded per-frame
- Instance color VBO: `[fillR,fillG,fillB, strokeR,strokeG,strokeB, opacity]` — 7 floats, static

### Visibility cache
`draw()` caches visible instance indices per shape group. Rebuilt only when frustum moves >0.5 units. Camera-relative offsets recomputed each frame on cached set.

### ColorTrack
Parsed via `processActions()` → per-tile `tileFillColors[]`/`tileStrokeColors[]`. Applied during `TileMesh::build()` as per-instance colors.

### Memory management
`LevelData::releaseMemory()` frees angleData, actions/decorations JSON, tilePositionOffsets, tileBPMs, tileFillColors, tileStrokeColors after loading.

### Tiles drawn without depth test
Z offset `2.0 - i*0.001` for basic layering. Icons drawn last with depth test off (always on top).

## Reference Implementations

- `../ADOFAI-JS/` — Core angle parsing
- `../Re_ADOJAS/` — Three.js web player (hitsound, camera, decorations)
- `../ADOFAN_PIXI/` — PixiJS web player
- `../ADOFAI/A Dance of Fire and Ice/` — Original Unity game. Use dnSpy to decompile `Assembly-CSharp.dll`
