# V0.5.0

First temporal-video prototype based on V0.4.6.

- Added OpenCV DIS optical-flow worker using a long-lived Python process.
- Flow direction is current frame -> previous frame.
- Added full-resolution `R16G16_FLOAT` motion-vector upload to Feature 18.
- Added `ProcessTemporal` path with one NGX Evaluate per real source frame.
- Added first-frame and scene-cut reset policy.
- Added motion-compensated Auto Depth stabilization.
- Added confidence-gated motion-compensated output stabilization.
- Added user-adjustable MV Scale X/Y for Feature 18 calibration.
- Added Legacy mode for direct A/B comparison with V0.4.6.
- Added CPU block-flow fallback for diagnostics.
- Added temporal CSV logging.
- Added less aggressive video defaults intended to reduce neural shimmer.
- Auto Build now runs FFmpeg and Auto Depth/Temporal setup from `dist`, so runtime dependencies are installed where the compiled executables actually look for them.
- OpenCV dependency is installed into the same runtime venv as Auto Depth.
- Historical Markdown files moved under `docs/`.
