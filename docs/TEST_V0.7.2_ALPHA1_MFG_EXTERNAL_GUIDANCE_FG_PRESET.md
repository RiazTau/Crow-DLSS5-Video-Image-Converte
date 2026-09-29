# V0.7.2-alpha1 — RTX Validation Plan

## A. Build validation

1. Start `BUILD.bat`.
2. Use CN or Global Full Build.
3. Confirm NVOF SDK validation is mandatory.
4. Confirm NVIDIA/DLSS, TinyEXR and NVAPI build dependencies resolve.
5. Confirm `nvngx_dlssg.dll` is staged into `dist/runtime`.
6. Confirm the main application is `dist/Crow-DLSS-Rendering-Tool.exe`.

## B. RTX 40-series policy test

On RTX 40-series hardware:

- enable FG;
- confirm the FG Multiplier selector exposes only **2X**;
- perform FG-only 2X and NR -> FG 2X conversions;
- confirm the output FPS is exactly source FPS x2;
- confirm completion and cancellation do not crash.

Any method that attempts >2X should still be rejected by backend capability validation.

## C. RTX 50-series MFG test

On RTX 50-series hardware:

- record the runtime-reported maximum generated-frame count;
- confirm the UI offers 2X through the architecture-policy maximum;
- test every multiplier the runtime actually accepts;
- verify output frame count and CFR duration;
- inspect REAL / FG / FALLBACK preview labels;
- test scene cuts at every multiplier;
- test normal completion and cancellation.

Do not infer 3X-6X correctness from RTX 40 testing.

## D. External EXR Motion

Using the same EXR sequence settings already supported by NR:

1. select External EXR temporal motion;
2. enable FG;
3. verify NVOF is not forced back on;
4. verify the sequence validates and motion is passed to FG;
5. compare against the NVOF baseline for direction/scale correctness.

## E. External EXR Depth / Auto Depth

Test separately:

- Zero Depth fallback;
- Auto Depth;
- External EXR Depth.

Verify depth inversion follows the existing NR convention and that NR + FG can consume the same selected EXR depth sequence.

## F. FG Model Preset

For each selection:

- Driver Default
- Preset A
- Preset B
- Latest

verify the application reports successful NVAPI DRS setup before NGX initialization. Driver Default should remove Crow's own FG override. If the executable already belongs to another NVIDIA profile, verify the existing owning profile is reused.

## G. Regression tests

Run:

- NR only;
- FG only;
- NR -> FG;
- NVOF guidance;
- External EXR guidance;
- Performance Mode default;
- Legacy Sync Safe diagnostic mode;
- Portable build and runtime checks.
