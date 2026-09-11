# V0.6.6-alpha2 — NVOF D3D12 Native Execute 实机测试

## 目标
alpha2 第一次真正调用 `NvOFExecuteD3D12`。本轮先验证 SDK 5.x ABI、D3D12 资源注册/Fence、4x4 硬件光流输出、S10.5 缩放与 current->previous 方向；暂不以性能为验收标准。

## 1. SDK 配置
在 alpha2 源码目录运行：

```bat
NVOF_SDK_SETUP.bat
```

选择 NVIDIA Optical Flow SDK 5.x 根目录。每个新的源码目录都应重新保存一次 SDK 路径；SDK 本体不会被复制进项目。

## 2. 编译
运行：

```bat
AUTO_BUILD_CN.bat
```

或：

```bat
AUTO_BUILD.bat
```

CMake 阶段应出现：

```text
NVOF SDK headers detected: 1
NVOF SDK 5.x D3D12 ABI probe: 1
NVOF D3D12 execute bridge ready: 1
```

如果 `ABI probe` 或 `execute bridge ready` 为 `0`，不要继续视频测试；保留完整构建日志并反馈。

## 3. Runtime Probe
运行：

```bat
NVOF_RUNTIME_SELFTEST.bat
```

预期关键项：

```text
Driver module  : FOUND
D3D12 export   : FOUND
Version export : FOUND
SDK headers    : DETECTED AT BUILD
Native bridge  : READY
```

## 4. Native Execute Self-Test
运行：

```bat
NVOF_EXECUTE_SELFTEST.bat
```

自检生成 640x360 确定性纹理，将当前帧相对上一帧平移 `+24 px X / +8 px Y`。由于项目内部 Motion Vector 约定为 `current -> previous`，预期中位向量约为：

```text
Expected flow  : (-24.000, -8.000) px current->previous
```

成功时最终应看到：

```text
RESULT         : PASS - real NvOFExecuteD3D12 produced correctly scaled current->previous motion
```

alpha2 容差用于第一轮硬件验证：X ±8 px、Y ±6 px。它主要捕获方向翻转、固定点遗漏 `/32`、资源/Fence/Execute 失败等结构性错误。

## 5. 短视频回归
只有 Execute Self-Test PASS 后，再启动视频转换器：

1. 选择 `NVIDIA Optical Flow - NVOF D3D12 Alpha`。
2. 先使用 10–30 秒、此前已验证可以处理的 1080p 素材。
3. 优先检查 Motion Preview、运动方向、镜头平移、遮挡边缘与 Scene Cut。
4. 再检查 DLSSNR 输出是否有异常拉扯、局部反向运动或跨切镜拖影。
5. 最后才记录 FPS / CPU / GPU 占用；alpha2 每对帧仍重复上传两张 RGBA 图并 CPU Readback，所以这些性能数据不代表最终 NVOF 后端。

## 反馈材料
发生任何错误时，优先提供：
- `AUTO_BUILD_CN.bat` / `AUTO_BUILD.bat` 完整输出；
- `NVOF_RUNTIME_SELFTEST.bat` 完整输出；
- `NVOF_EXECUTE_SELFTEST.bat` 完整输出；
- 若已进入视频测试，再提供转换日志和 Motion Preview 截图。

## V0.6.6-alpha2 ABGR/BGRA D3D12 surface hotfix

- Fixed the first RTX 4090 Laptop native-video validation failure: the D3D12 driver reports `DXGI_FORMAT_B8G8R8A8_UNORM` (87) for the ABGR8 input usage rather than `DXGI_FORMAT_R8G8B8A8_UNORM`.
- The bridge now obeys `nvOFGetSurfaceFormatD3D12` instead of hard-coding RGBA8, prefers the validated BGRA8 DXGI surface, and swizzles the converter's internal RGBA bytes to BGRA at the NVOF upload boundary.
- RGBA8 is retained only as a compatibility fallback when a driver explicitly advertises it. NV12/R8 remain reported diagnostics rather than silently changing the initialized ABGR8 contract.
