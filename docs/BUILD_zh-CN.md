# Crow - DLSS Rendering Tool 构建入口

源码根目录只提供一个用户构建入口：`BUILD.bat`。

## 菜单

1. Unified NR + FG full build - CN mirrors：中国大陆镜像完整构建。
2. Unified NR + FG full build - Official/global sources：官方/国际源完整构建。
3. FG diagnostic standalone build - CN mirrors：FG 诊断版中国大陆镜像构建。
4. FG diagnostic standalone build - Official/global sources：FG 诊断版官方源构建。
5. Portable build - CN mirrors：Portable 中国大陆镜像构建。
6. Portable build - Official/global sources：Portable 官方源构建。

NVOF 和 DLSS-G Runtime 不再作为独立菜单项。

## 每次构建的必要步骤

### 1. NVIDIA Optical Flow SDK 5.x

所有 Full / FG Diagnostic / Portable 构建都会先验证 NVOF SDK。

- 如果 `.deps/nvof-sdk.path` 指向有效 SDK，自动复用。
- 如果没有有效配置，构建会打开官方 NVIDIA 下载页并要求选择已经解压的 SDK 根目录。
- 如果取消选择，构建直接停止；不会继续生成缺少 NVOF 支持的半成品。
- NVOF SDK 仅用于编译，不复制到 `dist` 或 Portable 包。

### 2. DLSS-G Runtime

构建会从已获取的 NVIDIA/DLSS SDK 中自动查找并部署：

`dist/runtime/nvngx_dlssg.dll`

CN 构建优先使用 Gitee NVIDIA/DLSS 镜像，Global 构建使用官方 GitHub 源。如果自动获取后仍找不到 Runtime，构建会强制要求用户选择可信的 `nvngx_dlssg.dll`；取消则构建失败。

### 3. DLSSNR Runtime

`nvngx_dlssnr.dll` 仍由用户选择经过验证的 Runtime；项目不会自动下载实验性 DLSSNR Runtime。

## 默认视频运行模式

直接运行 `Crow-DLSS-Rendering-Tool.exe` 时默认使用 **Performance Mode**：

- D3D12 batching：开启
- CPU worker：自动

仅在诊断时使用 `dist/tools/VIDEO_LEGACY_SYNC_SAFE_MODE.bat` 才进入旧的单线程/同步安全路径。

## 命令行

```bat
BUILD.bat full-cn
BUILD.bat full
BUILD.bat fg-cn
BUILD.bat fg
BUILD.bat portable-cn
BUILD.bat portable
```

这些命令同样执行必要的 NVOF 与 DLSS-G 检查。

## dist 规范

```text
dist/
├─ Crow-DLSS-Rendering-Tool.exe
├─ Crow-DLSS-Rendering-Tool-Image.exe
├─ runtime/
│  ├─ nvngx_dlssnr.dll
│  └─ nvngx_dlssg.dll
├─ tools/
├─ video/
├─ auto_depth/
└─ models/
```

`dist` 顶层不放 CLI、自检 EXE、BAT、CMD 或 PowerShell 测试脚本；它们统一进入 `dist/tools/`。
