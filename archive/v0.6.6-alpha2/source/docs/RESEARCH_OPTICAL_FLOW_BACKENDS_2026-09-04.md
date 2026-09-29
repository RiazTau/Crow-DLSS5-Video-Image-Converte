# Optical-flow backend research — 2026-09-04

Purpose: identify better automatic motion-vector backends for DLSSNR temporal guidance while keeping the current converter deployable.

## Current V0.6.5.3 decision

Keep OpenCV DIS as the built-in zero-new-dependency backend, but make it stability-first and confidence-adaptive. Do **not** add a heavyweight neural runtime in this maintenance release.

## NVIDIA Optical Flow SDK 5.x (NVOFA)

Best next production backend for this project.

Reasons:
- RTX/Turing-and-newer GPUs contain a dedicated optical-flow accelerator independent of CUDA/graphics cores.
- Native DirectX 12 interface fits the converter's existing D3D12 architecture.
- Supports forward + backward flow in one execute call.
- Can expose output cost, global flow, temporal hints, external hints and 1x1 grids on supported Ampere+ hardware.
- Designed for video motion estimation and robust intensity changes.

Recommended future architecture:

`Auto Motion Backend = Adaptive DIS | NVIDIA Optical Flow`

NVOF should be optional and capability-detected. Do not route it through OpenCV's older `NvidiaOpticalFlow_2_0` wrapper; evaluate direct SDK 5.x D3D12 integration so current SDK features remain available.

## SEA-RAFT

Best near-term learned HQ candidate.

Pros:
- strong Spring/generalization accuracy;
- official demo exposes flow visualization and uncertainty;
- BSD-3-Clause project;
- substantially more accurate than classical DIS on difficult non-rigid/large-motion content.

Cons:
- PyTorch/CUDA/model-weight deployment is much heavier than the current OpenCV helper;
- GPU inference competes with DLSSNR instead of using a dedicated optical-flow engine;
- would require model/runtime version management and likely ONNX/TensorRT work for a clean Windows distribution.

Suggested future role: `Neural HQ Motion (experimental/offline)` rather than default Auto.

## WAFT

Very strong 2025/2026 research candidate with lower memory than cost-volume approaches and top published benchmark positioning. At present it requires modern PyTorch/CUDA/xFormers and is newer/less production-proven than SEA-RAFT. Track, but do not make it the first learned backend.

## GMFlow / UniMatch

Important reference for global matching and bidirectional consistency. GMFlow can generate bidirectional flow and explicitly supports forward/backward consistency checks, which closely matches the converter's need for confidence/occlusion diagnostics. Deployment is still substantially heavier than DIS/NVOF.

## MemFlow / VideoFlow-style multi-frame methods

Conceptually very relevant because the converter processes a continuous video, not independent pairs. Memory across frames can improve temporal stability and reduce pair-to-pair flow jitter. However it introduces a learned recurrent state and a much larger inference stack. Use as research reference for future temporal conditioning rather than immediate integration.

## RIFE

Excellent frame-interpolation system but not selected as the primary motion-vector backend. Its internal intermediate-flow representation is optimized for interpolation and does not automatically make it the best externally-consumed current-to-previous motion guide for DLSSNR.

## Development priority

1. V0.6.5.3 Adaptive Stable DIS — implemented.
2. Direct NVIDIA Optical Flow SDK D3D12 prototype + self-test — best next backend if Auto Motion still limits Cinematic stability.
3. SEA-RAFT/WAFT experimental offline HQ backend only if hardware OF quality is insufficient.
4. Multi-frame learned flow research after the single-pair backends are benchmarked.

## Benchmark required before replacing the default

Use the same troublesome video segments and compare:

- temporal deformation under Natural and Cinematic;
- forward/backward reprojection error;
- low-confidence fraction;
- frame-to-frame flow-field jitter;
- moving-edge accuracy / disocclusion;
- flow processing ms/frame;
- end-to-end conversion FPS;
- GPU utilization and VRAM.

Do not replace Adaptive DIS as default solely on Sintel/KITTI/Spring leaderboard values; the actual objective is stable DLSSNR guidance on decoded video.
