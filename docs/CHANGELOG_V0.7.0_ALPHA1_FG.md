# Crow - DLSS Rendering Tool — V0.7.0-alpha1 FG

## Scope

This alpha introduces DLSS Frame Generation as a **standalone executable and UI**. It intentionally does not modify the stable V0.6.6-alpha2 image/video application workflow.

Executable:

`dist/Crow-DLSS-Rendering-Tool-FG.exe`

## Alpha1 pipeline

`FFmpeg RGBA decode -> NVIDIA Optical Flow (D3D12) -> DLSS Frame Generation 2X -> FFmpeg/NVENC encode`

- DLSS-G multiplier is fixed at **2X** (`multiFrameCount=1`, `multiFrameIndex=1`).
- Motion guidance is the existing full-resolution **current-to-previous, pixel-unit** NVOF field.
- Depth is an explicit **zero-depth alpha guidance** buffer. This is a bring-up contract, not equivalent to native game-engine geometry depth.
- Hard scene cuts reset temporal history. If the SDK disables interpolation on a reset/cut frame, the converter inserts a conservative duplicate timing frame so CFR duration remains stable.
- Output timeline is exactly `2 * source frame count` at `2 * source nominal FPS` for the current CFR-oriented alpha pipeline.
- Source audio is remux-input and re-encoded as AAC by the FFmpeg writer in alpha1.

## Standalone UI

The FG UI contains only controls relevant to this experiment:

- Input/output video
- Fixed DLSS Frame Generation 2X mode
- Encoder: H.264 NVENC / HEVC NVENC / H.264 CPU
- NVOF quality, output grid, temporal hints and cost output
- Scene-cut threshold
- Runtime capability check
- Start / lifecycle-safe Cancel
- Progress and diagnostic log

Closing the window during conversion requests cancellation first and destroys the UI only after worker/GPU/process cleanup completes.

## Dependencies

1. Visual Studio 2022 / MSVC x64 + CMake
2. Current official `NVIDIA/DLSS` SDK checkout with DLSS-G helper and `nvngx_dlssg.dll`
3. NVIDIA Optical Flow SDK 5.x headers (configured by `NVOF_SDK_SETUP.bat`)
4. NVIDIA driver exposing `nvofapi64.dll`
5. FFmpeg/ffprobe (the existing video setup path is reused)

No NVIDIA Optical Flow SDK headers or licensed runtime files are redistributed in the source package beyond what NVIDIA publishes through its official DLSS repository and what the user stages locally.

## Build / run

1. Run `NVOF_SDK_SETUP.bat` once and select the extracted NVIDIA Optical Flow SDK 5.x root.
2. Run `BUILD_FG.bat`.
3. Run `RUN_FG.bat` or launch `dist/Crow-DLSS-Rendering-Tool-FG.exe`.
4. Press **Check Runtime** before the first conversion.

## Known alpha limitations

- Not hardware-validated in this source-generation environment; Windows RTX real-machine validation is required.
- Zero-depth guidance can cause foreground/background disocclusion artifacts.
- Variable-frame-rate sources are treated through a nominal CFR timeline in alpha1.
- Alpha1 uses CPU readback between DLSS-G output and FFmpeg rawvideo input; a later zero-copy/NVENC path can remove this bottleneck.
- HDR is not preserved; alpha1 uses RGBA8 SDR input/output.
- 3X+ MFG is intentionally hidden until the 2X lifecycle/output contract is validated.
