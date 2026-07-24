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
- [ ] ColorTrack（COLOR_FUNCS / FLOOR_FUNCS / RecolorTrack）

## 渲染
- [x] Per-instance color (iColor/iBgColor/iOpacity)
- [x] Split instance VBO: pos (3f per-frame) + color (7f static)
- [x] Visibility cache: rebuild only on frustum change (position + zoom)
- [x] Sort instances descending (Re_ADOJAS draw order)
- [x] Camera-relative offsets on CPU (removed uCam from shader)
- [x] Event icons: Twirl (purple), SetSpeed (red/blue)
- [x] depthWrite + renderOrder (Z-depth: far=200, tile Z 0~9, 755K depth steps)
- [x] `setPoints` trail: buffer under-allocation fixed (segsPerPoint=4 factor)
- [ ] 中旋（angleData=999）渲染：应为圆角五边形（原版 curvaturePoints=3），当前大圆+菱形像发卡弯
- [ ] MoveTrack (tile position/rotation/scale animation)
- [ ] GPU compute culling（已回退，ADOCAV 参考）
- [x] Multithreaded CPU culling (>= 64 groups → std::async parallel)

## 功能
- [x] JSON cleaner: Python literals, missing commas
- [x] angleData double precision (16 decimal places)
- [x] Memory: releaseMemory() after loading
- [x] OpenGL 4.3 upgrade + compute shader functions (GPU culling ready)
- [x] `--auto-play` CLI flag
- [x] DPI awareness + CPU pin to big cores
- [x] Planet trail: Catmull-Rom spline + GPU rendering
- [x] CMake options: EXTRA_ZOOM, PORTABLE
- [ ] MoveCamera (5 relativeTo modes)
- [ ] PositionTrack: relativeTo, rotation, scale, opacity, stickToFloors
- [ ] Bloom / Flash 特效
- [ ] Decoration 系统

## 架构
- [x] GameWindow 重构为类（init/update/render 分离）
- [x] dirty check 跳过静止帧 GPU 上传（已回退：导致静止帧黑屏）
- [x] Spatial grid 加速大关卡剔除（已回退：queryGrid 从未接入 draw，复杂度和 O(n) 遍历无差距）
- [ ] 帧率上限从编译期常量改为运行时配置（LauncherConfig 选项）
- [ ] 缩放范围可配置（min/max zoom）
- [ ] 剔除边距可配置

## 性能
- [x] Visibility cache fix
- [x] Frame profiler (per-section timing)
- [x] High-FPS build option (1000fps)
- [x] Sleep-based frame pacing (CPU-efficient)
- [x] processActions() O(n+m) optimization (removed O(n*m) per-event fills)
- [x] Multithreaded CPU culling (parallel frustum test + offset compute)
- [x] precalculateTiming 多线程化

## 音频
- [x] Pause stops audio device (not just music)
- [x] Remove 100MB skip-loading-window threshold
- [x] 16-bit hard-clip hitsound mixing

## WinUI3 启动器
- [ ] `../ADOCAO_WinUI3_Launcher` — 完成 UI + 自包含部署

## CI
- [x] GitHub Actions: Windows (MinGW) + Linux, build + release
