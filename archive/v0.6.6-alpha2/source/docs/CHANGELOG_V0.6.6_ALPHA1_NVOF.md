# V0.6.6-alpha1 — NVIDIA Optical Flow D3D12 Foundation

This alpha starts the NVOF backend without replacing the accepted DLSSNR/Temporal pipeline.

## Implemented in alpha1

- New persistent Temporal option: `NVIDIA Optical Flow - NVOF D3D12 Alpha` (index 4; old saved indices 0-3 are unchanged).
- Runtime probe dynamically loads driver-installed `nvofapi64.dll` and verifies `NvOFAPICreateInstanceD3D12`.
- New `Crow-DLSS5-Video-Image-Converter-NVOF-Self-Test.exe` reports D3D12 adapter, driver runtime/export availability, SDK-header discovery and bridge state.
- Optional `NVOF_SDK_DIR` CMake path. Proprietary NVOF SDK files are not bundled.
- `NVOF_SDK_SETUP.bat` opens the official NVIDIA download page, lets the user select an already accepted/extracted SDK, and stores only a local path pointer.
- New portable NVOF postprocessor:
  - decodes signed S10.5 fixed-point flow (`raw / 32`);
  - treats hardware call `input=current, reference=previous` as the converter-native current->previous direction;
  - upsamples coarse hardware vector grids without incorrectly multiplying vector magnitude by grid spacing;
  - combines optional 8-bit NVOFA cost, forward/backward consistency and photometric reprojection into per-pixel confidence;
  - preserves scene-cut reset semantics.
- VideoConverter plumbing for a dedicated `NvofFlowSession` is isolated from Adaptive DIS, CPU flow and External EXR.
- Flow Width is disabled in the GUI when NVOF mode is selected because native NVOF grid/resolution policy will be owned by the hardware backend rather than the DIS analysis width.

## Intentionally not enabled in alpha1

The vendor-header-specific `NvOFRegisterResourceD3D12` / `NvOFExecuteD3D12` bridge is gated with `DLSS5_NVOF_D3D12_BRIDGE_READY=0`.

Reason: the current development environment does not contain the NVIDIA Optical Flow SDK package. NVIDIA requires the developer to accept its SDK license before download, so this project does not fetch or redistribute the proprietary headers/samples. The bridge should be compiled and corrected against the user's actual accepted SDK package on Windows/MSVC rather than guessing the ABI.

Selecting NVOF in alpha1 therefore fails explicitly with an actionable message instead of silently falling back to DIS.

## Alpha2 target after SDK is supplied

1. Include official `NvOFInterface/nvOpticalFlowD3D12.h`.
2. Load/populate `NV_OF_D3D12_API_FUNCTION_LIST` through `NvOFAPICreateInstanceD3D12`.
3. Create `NvOFHandle` from the existing `ID3D12Device`.
4. Query caps and choose 1x1/2x2/4x4 output grid supported by GPU.
5. Initialize Optical Flow mode with SLOW quality, output cost, forward+backward, global flow and temporal hints.
6. Maintain a pool of D3D12 input/reference resources and registered NVOF handles.
7. Execute with explicit D3D12 fence points.
8. Read forward/backward packed S10.5 vectors + UINT8 cost and feed `PostprocessNvofFlow()`.
9. Invalidate temporal hints on scene cut.
10. Add NVOF cost/confidence diagnostics to the existing Motion Preview/logging.

The stable Direct NGX DLSSNR Feature 18 core is not modified by this alpha.
