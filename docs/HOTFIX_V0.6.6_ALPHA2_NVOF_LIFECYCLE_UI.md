# V0.6.6-alpha2 — NVOF Lifecycle / Cancellation / Tuning UI Hotfix

## Real-machine symptom

After the ABGR/BGRA input-surface hotfix, native NVOF D3D12 video conversion could run normally on the RTX 4090 Laptop validation machine, but the application could terminate unexpectedly when:

1. the user pressed **CANCEL** during an NVOF conversion; or
2. an NVOF video conversion reached normal completion and the worker unwound its local objects.

## Root-cause hardening

Two lifetime hazards were removed.

### 1. Thread-wide cancellation was unsafe for a worker that can be inside NVOF/D3D12

The GUI previously called `CancelSynchronousIo(worker.native_handle())`. That API targets *all* cancellable synchronous I/O issued by the worker thread. Once the same thread also enters NVIDIA driver/runtime calls, a thread-wide cancellation is too broad for safe GPU-session teardown.

The main conversion worker now uses cooperative cancellation only. The cancel flag is observed by the converter/FFmpeg loops, and the current GPU/FFmpeg operation is allowed to return before NVOF resources are released.

### 2. D3D12 NVOF cleanup order now follows the SDK contract

The original alpha2 destructor unregistered NVOF handles but kept the client `ID3D12Resource` objects alive until after `NvOFDestroy`, because C++ member destruction happened after the destructor body.

The hotfix now performs an explicit idempotent `Shutdown()` sequence:

1. wait for the most recent NVOF fence;
2. flush the application's D3D12 queue;
3. unregister NVOF GPU buffer handles;
4. release the client D3D12 input/output/cost resources;
5. call `NvOFDestroy`;
6. release the NVOF synchronization fence/event;
7. unload `nvofapi64.dll`.

`ConvertVideo` explicitly resets the `NvofFlowSession` on cancellation, on error, and after successful encoder finalization, while the shared `D3D12Context` is still alive.

## NVOF tuning UI

Four NVOF-specific controls are now exposed and persisted in `video/video-parameters.ini`:

- **NVOF Quality**
  - Quality / Slow (default)
  - Balanced / Medium
  - Performance / Fast
- **NVOF Grid**
  - 4x4 - Validated Stable (default)
  - 2x2 - Finer
  - 1x1 - Finest
- **NVOF Temporal Hints** — enabled by default. Scene cuts still force hint invalidation even when enabled.
- **NVOF Output Cost** — enabled by default. When disabled, the postprocessor uses its neutral confidence fallback and no D3D12 cost buffer is allocated/registered/read back.

Grid support is validated against `NV_OF_CAPS_SUPPORTED_OUTPUT_GRID_SIZES` before `NvOFInit`; selecting a grid unsupported by the active GPU/driver produces an explicit error rather than silently changing the requested setting.

## Compatibility

- Adaptive Stable DIS remains the application default.
- Existing Temporal mode enum values are unchanged.
- Existing V0.6.4 parameter persistence remains backward compatible; the four NVOF settings extend the profile instead of changing old keys.
- ABGR/BGRA driver-negotiated input-surface handling is retained.
