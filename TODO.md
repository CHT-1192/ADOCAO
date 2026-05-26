# TODO

## 打拍音
- [ ] `setPoints` 蓝球位置正确但渲染不可见——debug VBO 上传管线
- [ ] 修好后启用 `computePlanetTrails` 替代旧版单点更新

## 性能
- [ ] precalculateTiming 多线程化（6.7M tile 主要瓶颈）
- [ ] Instance culling 空间分块加速（当前 O(n) per-shape）

## 功能
- [ ] MoveCamera 事件（5 种 relativeTo 模式）
- [ ] MoveTrack / RecolorTrack / ColorTrack
- [ ] Bloom / Flash 特效
- [ ] Decoration 系统
- [ ] positionTrack: relativeTo / rotation / scale / opacity / stickToFloors
- [ ] 多语言 UI

## WinUI3 启动器
- [ ] `../ADOCAO_WinUI3_Launcher` — 完成 UI + 自包含部署
