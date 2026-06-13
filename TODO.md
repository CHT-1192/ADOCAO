# TODO

## 打拍音
- [x] Phase 1-2: per-instance color + dirty check
- [x] Multi-type support (TimestampGroup, SetHitsound)
- [x] 16-bit hard-clip matching HitSoundGenerator.exe
- [x] Case-insensitive hitsound type matching
- [x] Unknown type fallback to default
- [x] `--force-hitsound` (GUI + CLI): None → Kick
- [x] Export button in launcher + `--export` CLI
- [x] ma_decoder: AIFF, OGG, WAV, FLAC support
- [ ] Phase 3: ColorTrack COLOR_FUNCS (Single, Glow, Blink, Switch, Rainbow, Stripes)
- [ ] Phase 3: ColorTrack FLOOR_FUNCS (Standard, Neon, NeonLight, Basic, Gems, Minimal)
- [ ] Phase 3: ColorTrack RecolorTrack runtime triggers

## 渲染
- [x] Per-instance color (iColor/iBgColor/iOpacity)
- [x] Split instance VBO: pos (3f per-frame) + color (7f static)
- [x] Visibility cache: rebuild only on frustum change (position + zoom)
- [x] Sort instances descending (Re_ADOJAS draw order)
- [x] Camera-relative offsets on CPU (removed uCam from shader)
- [x] Event icons: Twirl (purple), SetSpeed (red/blue)
- [ ] depthWrite + renderOrder (reverted — Z-fighting with overlap)
- [ ] `setPoints` trail: `computePositionsAtTime` returns positions but render broken
- [ ] MoveTrack (tile position/rotation/scale animation)
- [x] Compute shader infrastructure (OpenGL 4.3, SSBO, indirect draw)
- [x] GPU compute offset: tile_offset shader computes camera-relative offsets on GPU
- [ ] GPU frustum culling with indirect draw (multi-draw, Phase 5)
- [ ] Spatial grid culling (O(1) vs current O(N) with cache)

## 功能
- [x] JSON cleaner: Python literals, missing commas
- [x] angleData double precision (16 decimal places)
- [x] Memory: releaseMemory() after loading
- [x] OpenGL 4.3 upgrade + compute shader functions (GPU culling ready)
- [x] `--auto-play` CLI flag
- [x] DPI awareness + CPU pin to big cores
- [ ] MoveCamera (5 relativeTo modes)
- [ ] PositionTrack: relativeTo, rotation, scale, opacity, stickToFloors
- [ ] Bloom / Flash 特效
- [ ] Decoration 系统

## 性能
- [x] Visibility cache fix
- [x] Frame profiler (per-section timing)
- [x] High-FPS build option (1000fps)
- [x] Sleep-based frame pacing (CPU-efficient)
- [ ] precalculateTiming 多线程化

## 音频
- [x] Pause stops audio device (not just music)
- [x] Remove 100MB skip-loading-window threshold
- [x] 16-bit hard-clip hitsound mixing

## WinUI3 启动器
- [ ] `../ADOCAO_WinUI3_Launcher` — 完成 UI + 自包含部署

## CI
- [x] GitHub Actions: Windows (MinGW) + Linux, build + release
