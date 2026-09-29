Crow - DLSS Rendering Tool
V0.7.0-alpha1 — Standalone 2X Frame Generation
构建热修复：Build Hotfix 1 修复 BUILD_FG.bat 将 Release 错误绑定为 NGX SDK 路径的问题。

================================================

定位
----
本版本首次加入 DLSS Frame Generation，但暂时作为完全独立的 EXE 与 UI 实现，
不会取代或改写 V0.6.6-alpha2 原有 Image / Video GUI。

目标 EXE：
  dist\Crow-DLSS-Rendering-Tool-FG.exe

当前管线
--------
视频解码（FFmpeg RGBA）
  -> NVIDIA Optical Flow / NVOF D3D12（当前帧 -> 前一帧运动向量）
  -> NVIDIA NGX DLSS Frame Generation
  -> 固定 2X（1 张生成帧 / 每对真实帧）
  -> FFmpeg / NVENC 编码

alpha1 Guidance：
  Motion = NVOF，full-resolution，current-to-previous，pixel unit
  Depth  = Zero Depth（实验性 bring-up，仅用于先验证离线 FG 技术链）

首次构建
--------
1. 下载并解压 NVIDIA Optical Flow SDK 5.x。
2. 双击 NVOF_SDK_SETUP.bat，选择 SDK 根目录。
3. 双击 BUILD_FG.bat。
4. 构建脚本会获取官方 NVIDIA/DLSS 依赖，并把官方 nvngx_dlssg.dll 放入 dist\runtime。
5. 双击 RUN_FG.bat。
6. UI 中先按 “Check Runtime”。
7. 选择视频与输出位置，再按 “Start 2X FG”。

重要限制
--------
- 这是 alpha1，需要 Windows + RTX 实机验证；当前源码生成环境不能完成 NVIDIA GPU 实机测试。
- 当前 Depth 为常量零深度，不等同于游戏引擎原生几何 Depth；遮挡/显隐区域可能出现瑕疵。
- HDR 暂未保留，当前链路为 RGBA8 SDR。
- VFR 视频当前按 nominal CFR 处理。
- 当前 DLSS-G 输出会读回 CPU 后再写入 FFmpeg；后续可继续做 GPU zero-copy / NVENC 优化。
- 3X/4X/5X/6X 暂不开放，先完成 2X 生命周期、帧序和视频导出验证。

取消/退出安全
-------------
运行过程中按 Cancel 或关闭窗口时，不会直接销毁程序；UI 会先设置取消标记，
让 FFmpeg、NVOF、DLSS-G 和 D3D12 完成清理，再退出。该设计专门避免 V0.6.6-alpha2
曾遇到的“取消转换/转换结束时闪退”问题重新出现。
