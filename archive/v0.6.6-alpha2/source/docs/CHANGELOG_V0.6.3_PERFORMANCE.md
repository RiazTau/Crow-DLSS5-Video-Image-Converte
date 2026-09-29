# V0.6.3 Performance Uplift Test

## Baseline
- Directly based on official V0.6.2.
- V0.6.2 remains the rollback baseline for V0.6.3.

## High-impact bounded optimizations
1. Parallel row execution for Full HQ denoise and full-resolution temporal helper loops.
2. Persistent D3D12 upload/readback staging resources.
3. One command-list submission and one fence wait per DLSSNR frame hot path instead of multiple upload/evaluate/readback waits.
4. Persistent motion-vector FP16 packing buffer with row-parallel conversion.
5. Per-stage video performance telemetry and A/B safe-mode launchers.

## Explicitly deferred
- Multi-frame asynchronous processing queues.
- Native NVDEC -> D3D12 -> NVENC zero-copy rewrite.
- Native DirectML Auto Depth integration.
- Replacing DIS Python helper with native OpenCV C++.

These are deferred because V0.6.3 is intended to establish measurable gains without broad architectural instability.
