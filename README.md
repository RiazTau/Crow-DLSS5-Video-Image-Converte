# Crow-DLSS5-Video-Image-Converter V0.6.6-alpha2 — Native NVOF D3D12 Execute Bridge

V0.6.6-alpha2 activates the first native NVIDIA Optical Flow DirectX 12 Execute path. The alpha1 runtime/SDK probe has passed on RTX 4090 Laptop GPU; alpha2 adds SDK-ABI-gated resource registration, explicit D3D12 fence synchronization, NvOFExecuteD3D12, cost readback and a synthetic motion self-test. The working default remains V0.6.5.3 Adaptive Stable DIS; the accepted Direct NGX DLSSNR Feature 18 / RTX40+RTX50 compatibility path is preserved.

## NVOF alpha status

The Video Converter now contains a fifth Temporal mode:

`NVIDIA Optical Flow - NVOF D3D12 Alpha`

Alpha2 enables the first native vendor-header `NvOFExecuteD3D12` bridge. CMake first compiles an SDK 5.x ABI contract probe; only a matching SDK enables `DLSS5_NVOF_D3D12_BRIDGE_READY=1`. The implementation then creates an NVOF context on the converter's existing NVIDIA D3D12 device, registers D3D12 resources, executes current->previous flow and reads the S10.5 vectors / UINT8 cost into the existing temporal postprocessor.

NVIDIA's Optical Flow SDK remains user-supplied. No proprietary NVOF SDK package, sample, library or driver DLL is redistributed in this source package.

## Prepare the NVIDIA Optical Flow SDK

1. Run `NVOF_SDK_SETUP.bat`.
2. The helper opens NVIDIA's official Optical Flow download page.
3. Download/accept/extract Optical Flow SDK 5.x yourself.
4. Select the extracted SDK root containing `NvOFInterface/nvOpticalFlowD3D12.h`.
5. Re-run `AUTO_BUILD.bat` or `AUTO_BUILD_CN.bat`.

Only the local SDK path is stored in `.deps/nvof-sdk.path`.

After building, first run `NVOF_RUNTIME_SELFTEST.bat`. It should report `Native bridge : READY`. Then run `NVOF_EXECUTE_SELFTEST.bat`; this performs a real `NvOFExecuteD3D12` call on a deterministic 640x360 translated pair and rejects wrong direction or fixed-point scale.

## NVOF native alpha2 data contract

The new C++ postprocessor is ready for the hardware bridge:

- NVOFA packed signed S10.5 motion is decoded as `raw / 32` pixels.
- The native hardware call uses `input=current` and `reference=previous`, so NVOFA forward flow is already the converter's required `current -> previous` convention.
- Alpha2 intentionally executes forward flow only. The postprocessor retains forward/backward consistency support for a later validation step.
- Optional 8-bit NVOFA cost is interpreted conservatively: higher cost lowers confidence.
- Photometric reprojection is a third confidence signal; cost alone is not treated as perfect confidence.
- Coarse vector grids are interpolated to full resolution without scaling vector magnitude by grid spacing.
- Scene-cut resets remain compatible with the existing denoiser/depth/DLSSNR history pipeline.

## Existing video modes retained

- Legacy reset every frame
- Adaptive Stable DIS (working default)
- CPU Block Flow fallback
- External EXR Motion / CG ground truth
- NVIDIA Optical Flow / NVOF D3D12 Alpha

The 2x2 Original / DLSS5 Output / Depth Guidance / Motion Vectors preview, image/video parameter persistence, per-parameter Reset, External EXR Depth/Motion auto calibration, Full HQ Temporal Denoise, AV1 resilience and V0.6.3 performance path are retained.

## Important files

- `src/video/NvofRuntimeProbe.*`
- `src/video/NvofFlowPostprocess.*`
- `src/video/NvofFlowSession.*`
- `src/video/NvofD3D12Bridge.*`
- `src/tools/NvofSelfTest.cpp`
- `src/tools/NvofExecuteSelfTest.cpp`
- `NVOF_EXECUTE_SELFTEST.bat`
- `scripts/setup_nvof_sdk.ps1`
- `docs/CHANGELOG_V0.6.6_ALPHA1_NVOF.md`
- `docs/CHANGELOG_V0.6.6_ALPHA2_NVOF_EXECUTE.md`
- `docs/RESEARCH_OPTICAL_FLOW_BACKENDS_2026-09-04.md`


## V0.6.6-alpha2 lifecycle + NVOF tuning hotfix

The first real-video NVOF path is now hardened for both normal completion and user cancellation. The GUI no longer applies thread-wide `CancelSynchronousIo` to the conversion worker; cancellation is cooperative so NVIDIA/D3D12 calls can finish before teardown. NVOF cleanup is explicit and ordered as fence/drain -> unregister -> release client D3D12 resources -> `NvOFDestroy`, and the NVOF session is explicitly reset before the worker reports completion.

When `NVIDIA Optical Flow - NVOF D3D12 Alpha` is selected, four persisted controls are available: **NVOF Quality** (Slow/Medium/Fast), **NVOF Grid** (4x4/2x2/1x1 with runtime capability validation), **NVOF Temporal Hints**, and **NVOF Output Cost**. The validated defaults remain Slow + 4x4 + hints on + cost on.

See `docs/HOTFIX_V0.6.6_ALPHA2_NVOF_LIFECYCLE_UI.md`.

## Next alpha milestone

After the real-video alpha2 path is stable, the next step is performance/quality hardening: GPU-resident ping-pong inputs, reduced CPU/GPU synchronization, and forward/backward consistency testing. 2x2/1x1 grids are now exposed for capability-gated real-machine validation, while 4x4 remains the validated default. Alpha2 performance should not yet be used as the final NVOF-vs-DIS benchmark because it intentionally uploads both RGBA frames and reads flow/cost back every pair.

## Retained V0.6.5.3 behavior

V0.6.5.3 **Adaptive Stable Motion** remains the working automatic-motion fallback and is unchanged in this alpha. Existing parameter persistence is also unchanged. There is deliberately no `Reset All` action.

## V0.6.6-alpha2 ABGR/BGRA D3D12 surface hotfix

- Fixed the first RTX 4090 Laptop native-video validation failure: the D3D12 driver reports `DXGI_FORMAT_B8G8R8A8_UNORM` (87) for the ABGR8 input usage rather than `DXGI_FORMAT_R8G8B8A8_UNORM`.
- The bridge now obeys `nvOFGetSurfaceFormatD3D12` instead of hard-coding RGBA8, prefers the validated BGRA8 DXGI surface, and swizzles the converter's internal RGBA bytes to BGRA at the NVOF upload boundary.
- RGBA8 is retained only as a compatibility fallback when a driver explicitly advertises it. NV12/R8 remain reported diagnostics rather than silently changing the initialized ABGR8 contract.

## Portable package generator

V0.6.6-alpha2 carries forward the validated V0.6.5.3 Portable Edition packaging path. The recommended mainland-China entry point is `BUILD_PORTABLE_CN.bat`; `BUILD_PORTABLE.bat` uses the standard download sources.

Before running the portable generator, configure the NVIDIA Optical Flow SDK with `NVOF_SDK_SETUP.bat`. The portable generator intentionally never downloads `nvngx_dlssnr.dll`; if no validated runtime is already present in `dist/runtime`, it opens the existing manual file picker and lets you select one before compilation.

The generated package keeps the converter binaries on `/MD` to remain ABI-compatible with NVIDIA NGX and deploys the matching x64 VC143 CRT app-local. The same CRT DLL set is also copied beside the embedded Python executable so OpenCV/ONNX Runtime extension modules remain usable on a clean Windows target without a separately installed VC++ Redistributable.

The portable tree contains FFmpeg/ffprobe, the Depth Anything V2 model, a relocatable x64 CPython environment with AutoDepth/DIS dependencies, the converter/self-test executables and the manually imported DLSSNR runtime. NVIDIA Optical Flow SDK headers are build-time only and are not redistributed; `nvofapi64.dll` continues to come from the target PC's NVIDIA display driver.

The final ZIP is created by `scripts/zip_portable.py` using Python `zipfile` + Zip64 rather than Windows PowerShell 5.1 `Compress-Archive`. `scripts/portable_stage.py` removes unused ONNX Runtime developer-tool paths and Python caches before archiving to avoid recurrence of the V0.6.5.3 long-path failure.

Expected output:

```text
portable/
  Crow-DLSS5-Video-Image-Converter-V0.6.6-alpha2-Portable-x64/
  Crow-DLSS5-Video-Image-Converter-V0.6.6-alpha2-Portable-x64.zip
```

See `docs/CHANGELOG_V0.6.6_ALPHA2_PORTABLE_GENERATOR.md` for the packaging contract.
