# Crow - DLSS Rendering Tool v0.7.3-alpha1

**Release type:** Alpha / source release
**Platform:** Windows x64 / NVIDIA RTX

## What is new

### Recursive DLSS5 image rendering

Image mode now exposes `DLSS5 Passes` from 1 to 8. With more than one pass, Crow sends the completed result back through DLSS5 again, allowing controlled recursive re-render experiments. The existing `Iterations` control remains independent.

### SEA-RAFT Runtime Hotfix5

This release carries the mature SEA-RAFT runtime repair path from the Hotfix5 lineage. It fixes PowerShell venv argument handling, validates CUDA PyTorch and torchvision separately, provides CN/official fallback behavior, uses compatible Hugging Face `strict=False` model loading, and smoke-tests cached Spring-S / Spring-M checkpoints.

## Retained features

- Unified DLSS Neural Rendering + FG/MFG image/video pipeline.
- Frame Generation / Multi Frame Generation with capability-driven multipliers and optional NVAPI FG preset support.
- Native NVIDIA Optical Flow path with multi-scale/predictive motion reconstruction.
- Optional BSD-3-Clause SEA-RAFT neural motion backend with Spring-S/Spring-M model, resolution, refinement, trust and uncertainty controls.
- External EXR Motion / Depth guidance shared with the rendering pipeline.
- Auto Depth using a pinned Depth Anything V2 Small ONNX model.
- FG model preset support when a valid NVAPI SDK is available.
- Image/video parameter persistence, motion preview, diagnostics and Portable build flow.

Third-party runtimes, SDKs, models and media tools retain their own licenses. The
expanded dependency and redistribution summary is in `docs/NOTICE.md`, including
PyTorch/torchvision, OpenCV, FFmpeg, NVIDIA Optical Flow SDK, NVIDIA NVAPI SDK and
Hugging Face-hosted model terms.

## Source-only release

The GitHub source archive intentionally excludes NVIDIA runtime DLLs/SDKs, PyTorch, SEA-RAFT upstream source and weights, FFmpeg binaries, downloaded model weights, compiled binaries, local environments, logs and user settings. Run `BUILD.bat` to prepare a local build.

## Validation

- Repository Python/contract suite: `48 passed`.
- SEA-RAFT worker Python syntax validation: PASS.
- Alpha status remains in effect; actual runtime compatibility depends on hardware, NVIDIA driver and external runtime/SDK versions.

## Suggested GitHub metadata

- **Tag:** `v0.7.3-alpha1`
- **Release title:** `Crow - DLSS Rendering Tool v0.7.3-alpha1`
- **Pre-release:** Yes
