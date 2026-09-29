# Current development status — V0.6.6-alpha2

V0.6.6-alpha2 is the first native NVIDIA Optical Flow (NVOFA) DirectX 12 Execute build. It preserves V0.6.5.3 Adaptive Stable DIS as the working default/fallback and the accepted DLSSNR compatibility path.

## alpha1 validation carried forward
- NVIDIA D3D12 adapter enumeration: PASS on RTX 4090 Laptop GPU.
- `nvofapi64.dll`: FOUND.
- `NvOFAPICreateInstanceD3D12`: FOUND.
- maximum API-version export: FOUND.
- Optical Flow SDK headers: DETECTED AT BUILD.

## alpha2 additions
- SDK 5.x ABI compile probe in CMake; the bridge is enabled only when the exact D3D12 structures/function-list members compile.
- Native `NvOFHandle` creation and 4x4 output-grid capability check.
- `NV_OF_MODE_OPTICALFLOW`, ABGR8 input, SLOW preset, 8-bit cost enabled.
- DirectX 12 input/output/cost resource registration with explicit `NV_OF_FENCE_POINT` synchronization.
- Native `NvOFExecuteD3D12` current->previous execution.
- R16G16_SINT S10.5 flow + UINT8 cost readback into existing NVOF postprocess.
- `Crow-DLSS5-Video-Image-Converter-NVOF-Execute-Self-Test.exe` synthetic translated-image test checks direction and fixed-point scale before real-video evaluation.
- Existing DIS / External EXR paths remain independent and unchanged.

## Alpha2 performance scope
The alpha2 correctness bridge uploads both current and previous RGBA frames for each Execute and performs CPU readback of flow/cost. This is intentional for first native validation and is not the final performance architecture. A later alpha will move to GPU-resident ping-pong input buffers / reduced synchronization and then benchmark NVOF vs DIS.

## V0.6.6-alpha2 ABGR/BGRA D3D12 surface hotfix

- Fixed the first RTX 4090 Laptop native-video validation failure: the D3D12 driver reports `DXGI_FORMAT_B8G8R8A8_UNORM` (87) for the ABGR8 input usage rather than `DXGI_FORMAT_R8G8B8A8_UNORM`.
- The bridge now obeys `nvOFGetSurfaceFormatD3D12` instead of hard-coding RGBA8, prefers the validated BGRA8 DXGI surface, and swizzles the converter's internal RGBA bytes to BGRA at the NVOF upload boundary.
- RGBA8 is retained only as a compatibility fallback when a driver explicitly advertises it. NV12/R8 remain reported diagnostics rather than silently changing the initialized ABGR8 contract.

## V0.6.6-alpha2 NVOF lifecycle / tuning hotfix (2026-09-07)

- Real NVOF D3D12 video execution confirmed operational on RTX 4090 Laptop after ABGR/BGRA surface negotiation hotfix.
- New real-machine defects reported: crash on user cancellation and crash after normal conversion completion.
- Main-worker thread-wide `CancelSynchronousIo` removed; cancellation is now cooperative to avoid interrupting NVIDIA/D3D12 driver calls.
- NVOF teardown now explicitly follows wait/drain -> unregister -> free client resources -> `NvOFDestroy`, with fail-safe leak behavior only for catastrophic teardown failures.
- `ConvertVideo` explicitly destroys the NVOF session before cancellation returns, error propagation, and normal completion notification.
- Added persisted NVOF UI controls: Quality (Slow/Medium/Fast), Grid (4x4/2x2/1x1), Temporal Hints, Output Cost.
- 4x4 + Slow + hints on + cost on remains the validated default.
