# V0.6.6-alpha2 — Native NVOF D3D12 Execute Bridge

## Purpose
Turn the alpha1 runtime/SDK foundation into the first real hardware optical-flow execution path while keeping the existing temporal backends intact.

## Native bridge
- Dynamically loads `nvofapi64.dll` and populates `NV_OF_D3D12_API_FUNCTION_LIST`.
- Creates `NvOFHandle` from the converter's NVIDIA `ID3D12Device`.
- Verifies 4x4 output grid support through `NvOFGetCaps`.
- Initializes Optical Flow mode with ABGR8 input, SLOW preset, forward prediction and 8-bit output cost.
- Queries D3D12 surface formats and requires RGBA8 input / R16G16_SINT flow output / 8-bit cost.
- Registers two input textures, one flow texture and one cost texture.
- Uses an independent `ID3D12Fence` for registration and Execute synchronization.
- Executes with `inputFrame=current` and `referenceFrame=previous`; NVIDIA forward flow is therefore already the converter's current->previous convention.
- Waits for NVOFA completion, reads flow/cost back, then reuses the alpha1 S10.5 decode and confidence postprocess.

## Build guard
CMake runs a compile-only SDK 5.x ABI contract probe. `DLSS5_NVOF_D3D12_BRIDGE_READY=1` is set only if the selected SDK exposes the exact alpha2 D3D12 fields/function pointers. No NVIDIA SDK headers are redistributed.

## Execute self-test
`NVOF_EXECUTE_SELFTEST.bat` launches a 640x360 synthetic translation test. A deterministic image is shifted +24 px X / +8 px Y, so expected current->previous flow is approximately (-24,-8) px. The test rejects wrong direction and the classic missing-/32 fixed-point scale error before real-video testing.

## Deliberate limitations
- 4x4 grid only in alpha2.
- Forward flow only; forward/backward single-call validation is deferred.
- Both RGBA inputs are uploaded each pair and output is CPU-read back; performance numbers from alpha2 are not representative of the final backend.
- Adaptive Stable DIS remains the default while NVOF receives real-video validation.

## V0.6.6-alpha2 ABGR/BGRA D3D12 surface hotfix

- Fixed the first RTX 4090 Laptop native-video validation failure: the D3D12 driver reports `DXGI_FORMAT_B8G8R8A8_UNORM` (87) for the ABGR8 input usage rather than `DXGI_FORMAT_R8G8B8A8_UNORM`.
- The bridge now obeys `nvOFGetSurfaceFormatD3D12` instead of hard-coding RGBA8, prefers the validated BGRA8 DXGI surface, and swizzles the converter's internal RGBA bytes to BGRA at the NVOF upload boundary.
- RGBA8 is retained only as a compatibility fallback when a driver explicitly advertises it. NV12/R8 remain reported diagnostics rather than silently changing the initialized ABGR8 contract.
