# V0.7.1-alpha1 Unified NR + FG — RTX Validation Plan

## Build

1. Extract the source to a short writable path.
2. Run `BUILD.bat`.
3. Use option 1 (CN mirrors) or option 2 (official/global sources). NVOF validation/selection now happens automatically as a required first build phase.
4. Confirm the build automatically stages DLSS-G, or prompts for `nvngx_dlssg.dll` if automatic acquisition is unavailable.
5. Confirm these files exist after build:
   - `dist/Crow-DLSS-Rendering-Tool.exe`
   - `dist/runtime/nvngx_dlssnr.dll`
   - `dist/runtime/nvngx_dlssg.dll`
   - `dist/tools/Crow-DLSS-Rendering-Tool-FG-Diagnostic.exe`

## Default execution mode

Launch `dist/Crow-DLSS-Rendering-Tool.exe` directly. It should run in Performance Mode by default. Use `dist/tools/VIDEO_LEGACY_SYNC_SAFE_MODE.bat` only for diagnostic regression comparison.

## Test media

Start with a short 2–5 second, 1920x1080, SDR, CFR 30 FPS H.264/HEVC clip. Avoid HDR, VFR, AV1 and 4K until the base path passes.

## A — NR regression

- NR: ON
- FG 2X: OFF

Expected:
- behavior remains equivalent to the established NR video path;
- output FPS equals source FPS;
- FINAL OUTPUT preview reports REAL frames;
- normal completion and Cancel do not crash.

## B — FG regression inside unified main app

- NR: OFF
- FG 2X: ON

Expected:
- temporal backend is forced to NVIDIA Optical Flow;
- 30 FPS source becomes nominal 60 FPS output;
- preview includes REAL and FG frames;
- output duration matches source duration;
- output-frame count is approximately 2x source count (exact CFR target for known frame count).

If this path fails while the standalone diagnostic succeeds, the regression is in unified-main integration rather than the basic FG runtime.

## C — Combined NR -> FG 2X

- NR: ON
- FG 2X: ON

Expected processing order:

`Source -> NVOF -> NR final real -> FG 2X -> final sequencer -> encoder/preview`

Confirm:
- both runtimes initialize;
- no NGX shutdown occurs while the other feature is still evaluating;
- preview shows the NR-processed REAL frames and generated FG frames;
- overlay changes between REAL / FG (and FALLBACK only when appropriate);
- output FPS doubles;
- audio/video duration does not drift;
- conversion completes cleanly;
- Cancel during conversion exits cleanly.

## D — Scene cut

Use a short clip containing at least one hard cut.

Expected:
- temporal reset is triggered;
- the CFR timeline is maintained;
- a FALLBACK frame may appear around the reset rather than a malformed cross-shot generated frame;
- no persistent temporal contamination after the cut.

## E — Preview behavior

The preview is diagnostic and decoupled from encoding. Its displayed Preview FPS may be lower than Output FPS because the UI samples frames and may drop preview updates. This is acceptable as long as the encoded stream does not drop frames.

## Failure data to preserve

If any stage fails, keep:

- the complete build/console text;
- `dist/logs` and video logs if generated;
- input probe information (codec/resolution/FPS);
- which switch combination failed (NR only / FG only / NR+FG);
- whether the standalone FG diagnostic still succeeds on the same machine.
