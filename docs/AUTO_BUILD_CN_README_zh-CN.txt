Crow-DLSS5-Video-Image-Converter V0.6.1.2 - 中国大陆镜像 AutoBuild
============================================================

入口：
  双击 AUTO_BUILD_CN.bat

目标：
  自动下载部分只使用中国大陆通常可访问的镜像服务；不自动回退到 GitHub、PyPI.org、Hugging Face 官方或 Gyan FFmpeg。

自动下载源：
  Python 安装器         USTC 镜像站 mirrors.ustc.edu.cn
  CMake Python wheel   USTC PyPI mirrors.ustc.edu.cn/pypi/simple
  MinGit               USTC GitHub Release 镜像
  NVIDIA DLSS/NGX SDK  Gitee mirrors_NVIDIA/DLSS
  TinyEXR              Gitee mirrors_syoyo/tinyexr
  FFmpeg / ffprobe     npmmirror ffmpeg-static 镜像（当前固定 6.1.1）
  Depth Anything V2    hf-mirror.com（固定模型版本 + SHA256 校验）

不会自动下载、需要用户手动处理的例外：
  1. Visual Studio 2022 Build Tools / Windows SDK
     原因：微软专有大型安装器，不使用来源不明的第三方镜像。
  2. Microsoft Visual C++ x64 Redistributable
     原因：专有系统运行库。脚本会检测 MSVCP140.dll，版本过旧时打开微软官方安装入口并停止。
  3. NVIDIA 显卡驱动
     原因：驱动属于高权限系统组件。缺失时打开 NVIDIA 中国驱动页面并停止。
  4. nvngx_dlssnr.dll
     原因：实验性/专有 runtime。本项目不自动获取；脚本弹出文件选择框，由用户提供。

PowerShell 执行策略：
  AUTO_BUILD_CN.bat 会使用 -ExecutionPolicy Bypass 启动 Windows PowerShell，脚本内部也会尝试将 Process scope 设为 Bypass。
  若企业/学校设备通过 MachinePolicy/UserPolicy 强制禁止脚本，本项目无法绕过组策略。请由管理员放行。

与普通 AutoBuild 的区别：
  AUTO_BUILD.bat     = 原始/国际网络路径
  AUTO_BUILD_CN.bat  = 中国大陆镜像路径

CN 版本还把运行时“Setup FFmpeg / Setup Auto Depth”按钮使用的 setup_video.ps1 与 setup_auto_depth.ps1 改为镜像版，并把 C++ ModelBootstrap 的 Depth Anything V2 URL 改为 hf-mirror，避免编译成功后 GUI 再回到海外下载源。

日志：
  logs\auto-build-cn-YYYYMMDD-HHMMSS.log

如某个镜像临时不可用，脚本会停止并给出具体阶段，不会静默回退到海外源。


V0.6.1.2 AV1 输入说明：
  - 转换器会在 DLSSNR 初始化前对 AV1 候选解码器进行 24 帧真实 RGBA + -xerror 预检。
  - 大陆镜像 FFmpeg 当前仍固定 npmmirror 6.1.1；脚本会显示 libdav1d / av1_cuvid / av1_qsv / libaom-av1 可用性。
  - 若镜像版本没有 libdav1d，程序仍会自动实测硬件、原生 AV1 与 libaom；仍失败时请查看 dist\video\logs\decoder-preflight-last.log。
  - AUTO_BUILD_CN 会把镜像版 setup_video_cn.ps1 同时复制为 dist\video\setup_video.ps1，确保 GUI 的 Setup Video Dependencies 按钮不会切回海外下载源。
