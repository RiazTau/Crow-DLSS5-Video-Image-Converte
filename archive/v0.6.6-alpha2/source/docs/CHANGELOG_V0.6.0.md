# V0.6.0 — Full HQ Denoise

## Added

- New `TemporalDenoiser` pre-DLSSNR stage.
- `Off / Temporal HQ / Full HQ` denoise modes.
- Motion-compensated recursive history using V0.5 current->previous optical flow.
- Bidirectional DIS flow consistency (current->previous checked against previous->current).
- Per-pixel confidence gating and scene-cut reset.
- YCoCg history clipping against the current 3x3 neighborhood.
- Local variance expansion so sensor/path-tracing noise is not treated as a hard disocclusion.
- Luma/chroma disagreement rejection.
- Motion-aware and detail-aware history weighting.
- Per-pixel history age (up to 24 frames by default).
- Edge-aware spatial fallback for areas with weak/rejected temporal history.
- Auto Depth now consumes the denoised color frame before existing depth stabilization.
- Extended temporal CSV diagnostics with denoise weights/rejection/history age.
- Portable C++ denoiser regression test.

## Preserved

- V0.5 DIS optical flow and scene-cut behavior.
- Real motion vectors into DLSSNR Feature 18.
- Temporal mode forces one DLSSNR evaluate per real video frame.
- Stable Depth / Stable Output switches.
- FFmpeg streaming pipeline without intermediate PNG frames.
- Standard and mainland-China AutoBuild variants.
- V0.5 AutoBuild fixes (`--config Release`, no BAT BOM, UAC bootstrap).

## Design goal

V0.5 primarily suppresses DLSSNR temporal shimmer. V0.6 additionally removes source noise before the neural pass so that random high-frequency noise does not become temporally unstable neural detail.
