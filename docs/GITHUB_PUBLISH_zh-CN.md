# Crow v0.7.3-alpha1 GitHub 发布步骤（网页端）

## 1. 仓库文件

把 `Crow-DLSS-Rendering-Tool-v0.7.3-alpha1-GitHub-Source.zip` 解压后，将其中的源码文件上传到仓库根目录。不要把 ZIP 本身当作仓库唯一内容上传。

确定使用的仓库名：

`Crow-DLSS-Rendering-Tool`

建议 Description：

`Experimental Windows RTX image/video rendering tool integrating DLSS Neural Rendering, FG/MFG, NVOF, SEA-RAFT, external Motion/Depth guidance and Auto Depth.`

建议 Topics：

`nvidia`, `dlss`, `frame-generation`, `optical-flow`, `sea-raft`, `directx12`, `rendering`, `image-processing`, `video-processing`, `windows`

## 2. 创建 GitHub Release

在仓库页面选择 **Releases → Draft a new release**。

- Tag：`v0.7.3-alpha1`
- Target：`main`
- Release title：`Crow - DLSS Rendering Tool v0.7.3-alpha1`
- 勾选：**Set as a pre-release**
- Release description：复制根目录 `RELEASE_NOTES_V0.7.3_ALPHA1.md` 的内容

## 3. Release Assets

建议至少上传：

- `Crow-DLSS-Rendering-Tool-v0.7.3-alpha1-GitHub-Source.zip`
- `Crow-DLSS-Rendering-Tool-v0.7.3-alpha1-SHA256SUMS.txt`

当前 GitHub 版是 source-only release，不要上传你个人使用的：

- `nvngx_dlssnr.dll`
- NVIDIA SDK 本地副本
- `.deps/`
- `dist/sea_raft/.venv/`
- SEA-RAFT 下载模型
- FFmpeg 下载目录
- Auto Depth venv / ONNX 权重
- 日志、用户参数、Token 或 Credential

## 4. GitHub Actions

`.github/workflows/windows-build.yml` 包含：

- Python / contract tests；
- SEA-RAFT worker Python syntax check；
- Windows x64 source compile check。

Windows CI 生成的 Artifact 是 **compile-only**，不包含 NVIDIA runtime、NVOF SDK、SEA-RAFT Runtime、FFmpeg 或模型，因此不要把它直接宣传成完整 Portable Release。

## 5. License

仓库保留 GPLv3 `LICENSE`。SEA-RAFT、PyTorch/torchvision、OpenCV、FFmpeg、NVIDIA Optical Flow SDK、NVIDIA NVAPI SDK、Hugging Face 下载模型及其他第三方组件仍遵循各自许可和分发条款，见 `docs/NOTICE.md`。
