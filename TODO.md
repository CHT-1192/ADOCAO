# TODO

## 打拍音
- [ ] Phase 1-2: per-instance color + dirty check (done)
- [ ] Phase 3: ColorTrack (basic parsing done — COLOR_FUNCS/Pulse/RecolorTrack runtime TBD)
- [x] ~~Multi-type support (TimestampGroup, SetHitsound)~~
- [x] ~~16-bit hard-clip matching HitSoundGenerator.exe~~
- [x] ~~Export button in launcher~~
- [x] ~~ma_decoder: AIFF, OGG, WAV, FLAC support~~

## 渲染
- [x] ~~Per-instance color (iColor/iBgColor/iOpacity)~~
- [x] ~~Split instance VBO: pos (3f per-frame) + color (7f static)~~
- [x] ~~Visibility cache: reuse culled set when camera static~~
- [x] ~~Sort instances descending (Re_ADOJAS draw order)~~
- [ ] depthWrite + renderOrder (reverted — Z-fighting with overlap)
- [ ] `setPoints` trail: `computePositionsAtTime` returns positions but render broken
- [ ] ColorTrack COLOR_FUNCS (Single, Glow, Blink, Switch, Rainbow, Stripes)
- [ ] ColorTrack FLOOR_FUNCS (Standard, Neon, NeonLight, Basic, Gems, Minimal)
- [ ] ColorTrack RecolorTrack runtime triggers
- [ ] MoveTrack (tile position/rotation/scale animation)
- [ ] Spatial grid culling (O(1) vs current O(N) with cache)

## 功能
- [x] ~~JSON cleaner: Python literals, missing commas~~
- [x] ~~angleData double precision (16 decimal places)~~
- [x] ~~Memory: releaseMemory() after loading~~
- [ ] MoveCamera (5 relativeTo modes)
- [ ] PositionTrack: relativeTo, rotation, scale, opacity, stickToFloors
- [ ] Bloom / Flash 特效
- [ ] Decoration 系统

## 性能
- [x] ~~Visibility cache fix~~
- [x] ~~Frame profiler (per-section timing)~~
- [x] ~~High-FPS build option (1000fps)~~
- [ ] precalculateTiming 多线程化
- [ ] GPU-side camera offset (uCam in shader, master branch already has this)

## WinUI3 启动器
- [ ] `../ADOCAO_WinUI3_Launcher` — 完成 UI + 自包含部署

## CI
- [x] ~~GitHub Actions: Windows (MinGW) + Linux, build + release~~
