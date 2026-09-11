# V0.6.6-alpha2 Lifecycle / Cancellation / NVOF UI real-machine test

Use the already validated RTX 4090 Laptop + Optical Flow SDK 5.x build environment.

## A. Build gate

1. Run `NVOF_SDK_SETUP.bat` and select the SDK root.
2. Run `AUTO_BUILD_CN.bat`.
3. Confirm:
   - `NVOF SDK headers detected: 1`
   - `NVOF SDK 5.x D3D12 ABI probe: 1`
   - `NVOF D3D12 execute bridge ready: 1`

## B. Baseline NVOF settings

Select `NVIDIA Optical Flow - NVOF D3D12 Alpha` and keep:

- NVOF Quality: `Quality / Slow`
- NVOF Grid: `4x4 - Validated Stable`
- NVOF Temporal Hints: ON
- NVOF Output Cost: ON

## C. Normal-completion regression

Convert a previously working short video all the way to 100%.

PASS requires:

- output file is valid;
- completion message is shown;
- application remains open and responsive after closing the completion message;
- a second conversion can be started in the same process.

## D. Cancellation regression

Start the same NVOF conversion and press CANCEL at three separate points in three runs:

1. early (first ~10%);
2. middle (~40-60%);
3. late (>85%).

PASS requires:

- status changes to safe cancellation / Cancelled;
- application does not terminate unexpectedly;
- partial output is removed;
- controls are re-enabled;
- another conversion can start without restarting the application.

Cancellation is cooperative. The current NVOF/D3D12 or FFmpeg operation may finish before teardown; this is intentional.

## E. NVOF Quality UI

With the same short input, run:

- Slow
- Medium
- Fast

All supported settings must initialize and complete. Record `flow_ms` / pipeline FPS for comparison, but alpha2 is not yet the final performance architecture.

## F. NVOF Grid UI

Run 4x4 first. Then test 2x2 and 1x1.

- If the GPU/driver reports support, the conversion should run and Motion Preview should remain directionally correct.
- If unsupported, the converter must show an explicit capability error and remain open; it must not silently fall back to 4x4.

## G. Temporal Hints

Test ON and OFF on a continuous camera-motion clip. OFF forces every pair to invalidate NVOFA temporal hints. ON remains the recommended continuous-video setting; scene-cut detection still invalidates hints automatically at cuts.

## H. Output Cost

Test ON and OFF.

- ON allocates/reads the NVOFA cost buffer and uses it in confidence.
- OFF must still convert successfully and must not report a cost-grid-size mismatch; postprocess uses neutral cost confidence.

## I. DIS regression

Switch Temporal back to `Adaptive DIS - Stable Default` and convert the same clip. It must behave exactly as before this hotfix.
