# V0.6.5.1 — External Data Auto Calibration

V0.6.5.1 is a focused upgrade over the V0.6.5 External Render Data + scroll-stability hotfix baseline. It does not alter the DLSSNR Feature 18 path, D3D12 batching, RTX40 compatibility layer, AV1 handling, Full HQ temporal denoiser, Auto Depth backend or encoder defaults.

## Depth auto calibration

The External Render Data editor now has **Auto Calibrate** for depth and **Auto Calibrate Both**. The analyzer scans representative frames across the numbered EXR sequence, evaluates every EXR channel, rejects constant/Alpha-like channels, recognizes common `Depth` / `Z` names and prefers R/G/B over a generic Alpha channel.

For metric/unbounded depth, one **global** robust P1–P99 range is derived from all sampled frames and written into Fixed Near/Far. It never normalizes every frame independently, so auto calibration does not introduce temporal depth breathing. If representative values are already consistently in 0–1, the analyzer selects Raw 0..1.

The result reports the selected channel, sampled frame count, P1/P50/P99 and a confidence level. Automatic depth orientation assumes the common far-high Z convention and remains manually invertible.

## Motion auto calibration

Motion calibration requires the source video selected in the main GUI. FFmpeg decodes a bounded low-resolution calibration window and keeps the most informative non-cut adjacent frame pairs. The analyzer then tests EXR channel pairs against real image reprojection.

Candidates cover:

- X/Y channel pairing and swapping;
- independent X/Y sign / Flip controls;
- full-resolution **pixel**, normalized **UV**, and **NDC** scales with local ×0.25/0.5/1/2/4 refinement;
- native `current -> previous` fields;
- `previous -> current` forward fields stored on the preceding EXR frame.

The winning candidate minimizes robust photometric reprojection error relative to the zero-motion baseline while applying a small semantic prior for conventional X/R and Y/G channels. The dialog reports error, zero-motion error and confidence. Low-confidence results remain fully editable rather than being treated as certain.

## Forward-flow runtime conversion

When auto calibration detects a previous-to-current source, V0.6.5.1 loads motion frame N-1 for current video frame N and converts it to the internal current-to-previous convention. Instead of scatter inversion with holes, each current pixel solves `p + F(p) = q` with bounded fixed-point iterations and bilinear flow sampling. The resulting current-to-previous field is then shared by the Full HQ denoiser, optional depth stabilization, DLSSNR Feature 18 and optional output stabilization exactly like native external motion.

## UI / persistence

The previous stable scroll-sidebar v2 implementation is retained. External calibration adds result/confidence fields and a Direction control. `motion_direction` is persisted with the other External Render Data processing parameters; EXR paths remain session-specific input media and are not saved.

## Validation

Portable regression coverage includes synthetic RGBA depth (constant Alpha + replicated metric RGB), native current-to-previous motion calibration, accelerated previous-to-current forward-flow detection, and forward-flow runtime inversion. Existing V0.6.5/V0.6.4/V0.6.3/RTX40/AV1/DIS/scroll contracts are retained. Windows/MSVC GUI and real S2R-HDR / Blender EXR validation remain the final machine-side checks.
