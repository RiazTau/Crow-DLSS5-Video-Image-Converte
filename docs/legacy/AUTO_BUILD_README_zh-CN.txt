Crow-DLSS5-Video-Image-Converter V0.6.1.2 - 一键自动源码编译
=================================================

入口
----
双击：AUTO_BUILD.bat

它会用 `PowerShell -ExecutionPolicy Bypass` 启动自动化脚本，因此通常不需要你永久修改系统的 PowerShell 执行策略。

自动执行内容
------------
1. 检测 Windows x64、GPU、NVIDIA 驱动状态。
2. 检测 Git、CMake、Visual Studio 2022 C++ Build Tools、Python 3.11+、VC++ x64 Runtime。
3. 缺少 Git / CMake / Python / VS Build Tools / VC++ Runtime 时，优先通过 winget 或微软官方地址自动安装。
4. 如果缺少 winget，会打开 Microsoft Store 的 App Installer 页面并提示手动安装。
5. NVIDIA 显卡驱动会自动检测；若明显缺失则打开 NVIDIA 官方驱动页面并要求手动安装后重跑。脚本不自动强制升级显卡驱动，因为实验性 DLSSNR 没有稳定公开的统一最低驱动版本，且驱动升级适合由用户确认。
6. 弹出文件选择窗口，让你选择合法获得的 `nvngx_dlssnr.dll`。脚本不会自动下载该 DLL。
7. 自动下载/验证 NVIDIA DLSS SDK 与 TinyEXR（沿用 V0.6.0 原 build.ps1 逻辑）。
8. 自动 Clean + Release 编译三个 EXE。
9. 自动把选择的 `nvngx_dlssnr.dll` 导入 `dist\runtime\`。
10. 默认自动准备 FFmpeg 与 Auto Depth / Temporal Python 环境/模型。
11. 生成 `logs\auto-build-*.log`。

PowerShell 执行策略
------------------
AUTO_BUILD.bat 会直接使用：

  powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\auto_build.ps1

并且脚本内部也尝试：

  Set-ExecutionPolicy -Scope Process Bypass -Force

这只影响当前进程，不永久修改 CurrentUser / LocalMachine。
如果公司/学校电脑通过 Group Policy 强制禁止脚本，普通脚本无法绕过管理员策略；此时需要管理员放行。

需要手动处理的少数情况
----------------------
- winget/App Installer 不存在：按提示从 Microsoft Store 安装 App Installer。
- Visual Studio Build Tools 自动安装后仍缺 C++ workload：按提示打开 Visual Studio Installer，安装“使用 C++ 的桌面开发”。
- NVIDIA 驱动缺失/异常：到 NVIDIA 官方驱动/NVIDIA App 手动安装或更新。
- `nvngx_dlssnr.dll`：必须由用户自己选择本地 DLL；脚本不提供、不下载实验性/专有 runtime。

可选参数
--------
从 PowerShell 运行：

  powershell -ExecutionPolicy Bypass -File .\scripts\auto_build.ps1 -SkipAutoDepth
  powershell -ExecutionPolicy Bypass -File .\scripts\auto_build.ps1 -SkipVideo
  powershell -ExecutionPolicy Bypass -File .\scripts\auto_build.ps1 -SkipRuntimePrompt
  powershell -ExecutionPolicy Bypass -File .\scripts\auto_build.ps1 -NoInstall

兼容性提示
----------
本包只负责自动准备环境和编译。当前实验性 DLSSNR Feature 18 是否能运行，由 GPU、NVIDIA 驱动以及你选择的 `nvngx_dlssnr.dll` 决定。
在你此前的实测中，RTX 5090 可创建 Feature 18；RTX 4090 Laptop 使用当前 runtime 时返回 0xBAD00001 / FeatureNotSupported。因此自动编译成功不等于所有 RTX GPU 都能运行 DLSSNR。


V0.6.1.2 AV1 解码兼容性修复
-------------------------
- AV1 输入在进入 DLSSNR 前执行 24 帧真实 RGBA 解码预检，并使用 FFmpeg `-xerror` 拒绝任何实际解码错误。
- 解码器按 libdav1d -> av1_cuvid -> av1_qsv -> 原生 av1 -> libaom-av1 -> FFmpeg 自动选择依次实测。
- 普通 AutoBuild 的 FFmpeg 安装脚本优先准备包含 libdav1d 的完整 Windows FFmpeg build。
- 新增 dist\video\logs\decoder-preflight-last.log；实际解码失败仍保留 dist\video\logs\decoder-last.log。
- 本修复不改变 V0.6.0 Full HQ Temporal Denoise、DIS 光流、Auto Depth、DLSSNR Feature 18 或编码链。

V0.6.0 AutoBuild 对原版的额外修复
--------------------------------
- build.ps1 明确使用 `Visual Studio 17 2022 -A x64`，避免 CMake 误选 NMake Makefiles。
- Auto Depth / Temporal setup 修复单元素 PowerShell 数组被拆成字符串后只执行 `p` 的问题。
- Auto Depth / Temporal 会检测从其他电脑复制来的失效 `.venv` 并自动重建。

V0.6.0 Full HQ 视频降噪功能：
- 推荐模式使用 OpenCV DIS Optical Flow。
- 新增 Full HQ 运动补偿时域降噪：历史帧根据 current->previous MV 重投影。
- 使用光流置信度、YCoCg 邻域夹取、亮度/色度差异拒绝和场景切换 Reset 抑制拖影。
- Full HQ 模式对低置信度/新显露区域增加边缘保护的 3x3 空域补偿。
- Auto Depth 改为从降噪后的颜色帧推理，再进行原有的时域深度稳定。
- 时域模式下 Iterations 自动固定为 1。
- 首帧/切镜 Reset=true，其余连续帧保留 Feature 18 历史。
- 日志：dist\video\logs\temporal-last.log，并新增 denoise_hist_w / denoise_spatial_w / denoise_reject / denoise_age 字段。
