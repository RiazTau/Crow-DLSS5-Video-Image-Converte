# V0.7.0-alpha1 — Standalone 2X FG Windows/RTX validation

## Purpose

Validate the first offline Direct NGX DLSS Frame Generation path before any 3X+ MFG, estimated depth, HDR or zero-copy work is added.

## Preflight

1. Configure Optical Flow SDK 5.x with `NVOF_SDK_SETUP.bat`.
2. Run `BUILD_FG.bat`.
3. Confirm:
   - `dist/Crow-DLSS-Rendering-Tool-FG.exe`
   - `dist/runtime/nvngx_dlssg.dll`
   - FFmpeg and ffprobe are discoverable by the existing video dependency path.
4. Start the FG EXE and press **Check Runtime**.
5. Expected: NVIDIA adapter name, `DLSS-G 2X`, nonzero max-generated-frame capability, and `Native NVOF D3D12 execute bridge compiled`.

## Test A — short CFR source

Use a 2–5 second SDR CFR clip, ideally 24/30 FPS and 1080p or lower for first validation.

Expected:

- output FPS = exactly input nominal FPS * 2;
- output frame count = approximately/exactly source decoded frame count * 2;
- output duration matches source nominal CFR duration;
- no crash at normal completion;
- `fg_logs/fg-last.csv` contains one row per source frame after the first;
- most non-cut rows report `dlss_generated=1` once temporal history is established.

## Test B — scene cut

Use a clip containing a hard cut.

Expected:

- scene-cut row reports `scene_cut=1`;
- DLSS history resets;
- if the SDK disables interpolation, `fallback=1` and the output remains CFR 2X;
- no interpolation between unrelated shots is emitted by the converter on that reset pair.

## Test C — cancel lifecycle

Start a longer conversion and press **Cancel** while NVOF/DLSS-G is active.

Expected:

- UI reports cancellation requested;
- no immediate process crash;
- worker drains/tears down GPU and FFmpeg resources;
- partial output is removed;
- UI returns to idle.

Repeat by closing the window during conversion. The window should request cancellation and close only after cleanup.

## Failure package

If the first RTX validation fails, preserve these files before rerunning:

- `dist/fg_logs/decoder-last.log`
- `dist/fg_logs/encoder-last.log`
- `dist/fg_logs/fg-last.csv`
- screenshot/text from **Check Runtime**
- GPU model and NVIDIA driver version
- whether failure happens at runtime check, CreateFeature, first Evaluate, later Evaluate, completion, or cancel.

Do not add 3X+ MFG or estimated depth until Test A + B + C pass on real hardware.
