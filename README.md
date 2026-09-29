# Crow - DLSS Rendering Tool

**Current public source release: `v0.7.3-alpha1`**
Windows x64 · NVIDIA RTX · Experimental / alpha software

Crow is an experimental Windows rendering/conversion tool that combines NVIDIA DLSS Neural Rendering, Frame Generation / Multi Frame Generation, NVIDIA Optical Flow, external render guidance, Auto Depth, and an optional SEA-RAFT neural optical-flow backend in one workflow.

> This GitHub package is **source-only**. NVIDIA runtime DLLs, the NVIDIA Optical Flow SDK, PyTorch, SEA-RAFT source/weights, FFmpeg binaries, downloaded model weights, and local virtual environments are intentionally not vendored.

## Feature overview

- **DLSS Neural Rendering for images and video**, including configurable image iterations.
- **Recursive DLSS5 image passes**: `DLSS5 Passes` can run `1–8` complete passes, feeding each completed image back into the next pass independently of `Iterations`.
- **Frame Generation / Multi Frame Generation (FG/MFG)** with capability-driven output multipliers and an optional NVAPI model preset path.
- **NVIDIA Optical Flow (NVOF)** with full-resolution and multi-scale motion paths, predictive reconstruction, confidence, visibility and uncertainty handling.
- **SEA-RAFT neural optical flow** with Spring-S / Spring-M models, selectable resolution/refinement controls and a CUDA PyTorch runtime.
- **External Motion / Depth guidance** from EXR sequences, including calibration and shared NR/FG guidance paths.
- **Auto Depth** using a pinned Depth Anything V2 Small ONNX model through ONNX Runtime DirectML.
- **Video processing and portable packaging** through FFmpeg/ffprobe, with diagnostics, parameter persistence and motion preview tooling.

## v0.7.3-alpha1 highlights

### Recursive DLSS5 image re-render

Image Conversion adds **`DLSS5 Passes`** with values `1–8` (default `1`). Each completed DLSS5 image output becomes the input to the next pass.

`DLSS5 Passes` is separate from the existing per-pass `Iterations` option. For example, `3 passes × 4 iterations` performs three complete image re-render passes, each using four internal evaluations.

### SEA-RAFT Runtime Hotfix5

The current source includes the restored, validated SEA-RAFT runtime fixes from the Hotfix5 lineage:

- avoids the PowerShell `$Args` automatic-variable collision;
- validates `.venv\Scripts\python.exe` immediately after venv creation;
- validates CUDA PyTorch and `torchvision` independently;
- provides mainland-China mirror-first installation with official fallback where appropriate;
- uses GitCode mirror-first checkout for SEA-RAFT with official GitHub fallback;
- uses `hf-mirror` first for model prefetch in CN mode, then official Hugging Face fallback;
- loads Hugging Face checkpoints with `RAFT.from_pretrained(..., strict=False)`;
- supports `--check-only` Spring-S / Spring-M model-load smoke tests.

### Existing rendering pipeline retained

The release keeps the established V0.7.2 alpha6/sr2 pipeline:

- unified NR, FG/MFG, and NR → FG/MFG video conversion;
- NVOF full + multi-scale motion paths with predictive/temporal reconstruction;
- optional SEA-RAFT tunable neural motion backend;
- External EXR Motion / Depth guidance;
- Auto Depth;
- capability-driven FG/MFG multiplier handling;
- optional NVAPI FG preset integration;
- persisted image/video parameters and motion preview tooling.


## Inherited architecture lineage

The current release retains the earlier Crow motion/rendering work rather than replacing it. The inherited lineage includes **Adaptive Stable Motion**, **Adaptive Reliable Motion**, **Temporal Consensus Motion**, **Spatial-Temporal Dual-Path Motion**, **Visibility-Aware Uncertainty Motion**, and **Predictive Multi-Scale Motion Reconstruction**. These names are preserved in the historical changelogs and contract tests.

Parameter persistence still uses per-control reset behavior. There is deliberately no `Reset All` action.

## Build

The only root build entry is:

```bat
BUILD.bat
```

Build Center options include:

1. Unified NR + FG full build — CN sources
2. Unified NR + FG full build — Official/global sources
3. FG diagnostic standalone — CN sources
4. FG diagnostic standalone — Official/global sources
5. Portable build — CN sources
6. Portable build — Official/global sources
7. Refresh NVIDIA NVAPI SDK

See [`docs/BUILD_zh-CN.md`](docs/BUILD_zh-CN.md) for the detailed Chinese build guide.

### Required / external components

A normal full build may require or acquire the following outside this repository:

- Visual Studio 2022 C++ Build Tools and Windows SDK;
- NVIDIA display driver;
- NVIDIA Optical Flow SDK 5.x;
- NVIDIA DLSS / NGX SDK checkout;
- user-supplied `nvngx_dlssnr.dll` appropriate for the installed GPU generation;
- DLSS-G runtime;
- TinyEXR;
- FFmpeg;
- Python and Auto Depth dependencies;
- CUDA PyTorch, SEA-RAFT source, and Spring-S / Spring-M weights when SEA-RAFT is installed.

The build scripts keep these downloaded/runtime components outside the tracked Git source through `.gitignore`.

## Output layout

A completed build uses the following structure:

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
├─ sea_raft/
└─ models/
```

## SEA-RAFT dependency boundary

SEA-RAFT is an independent Princeton Vision & Learning Lab project licensed separately under BSD-3-Clause. Crow ships an integration adapter and setup/runtime worker, but this source release does **not** vendor the upstream SEA-RAFT repository, PyTorch, or model weights. See [`sea_raft/README_zh-CN.md`](sea_raft/README_zh-CN.md) and [`docs/NOTICE.md`](docs/NOTICE.md).

## Validation status

The v0.7.3-alpha1 source package passed the repository Python/contract suite used for this release (`48 passed`) and Python syntax validation of the SEA-RAFT worker. This remains alpha software; GPU/runtime behavior depends on the installed NVIDIA driver, runtime DLLs, SDKs, and hardware.

## Release history

Detailed historical design notes, tests, and hotfix documents are kept under [`docs/`](docs/). Current release-specific notes:

- [`docs/CHANGELOG_V0.7.3_ALPHA1_STRICT_IMAGE_ONLY_REWRITE.md`](docs/CHANGELOG_V0.7.3_ALPHA1_STRICT_IMAGE_ONLY_REWRITE.md)
- [`docs/HOTFIX_V0.7.3_ALPHA1_SEA_RAFT_RUNTIME_HOTFIX5.md`](docs/HOTFIX_V0.7.3_ALPHA1_SEA_RAFT_RUNTIME_HOTFIX5.md)
- [`RELEASE_NOTES_V0.7.3_ALPHA1.md`](RELEASE_NOTES_V0.7.3_ALPHA1.md)

## License

Crow source is distributed under the **GNU General Public License v3.0**. Third-party components retain their own licenses. See [`LICENSE`](LICENSE) and [`docs/NOTICE.md`](docs/NOTICE.md).
