# Crow - DLSS Rendering Tool V0.7.2-alpha4
## Spatial-Temporal Dual-Path Motion

V0.7.2-alpha4 combines the two motion-quality directions explored after alpha3 and extends the result to both Neural Rendering (NR) and Frame Generation (FG).

## Spatial prior

When NVIDIA Optical Flow is selected and Motion Stabilization is Auto/Strong, Crow now runs a second NVOF session at half source resolution. Fine repetitive structure is reduced before this pass, making the coarse field a useful independent vote on fences, blinds, rails, dense windows and other periodic textures.

The coarse field is scaled back into full-resolution pixel units and fused conservatively with the normal full-resolution NVOF field. It is not allowed to blindly replace high-confidence full-resolution motion. Strong accepts the spatial prior more readily than Auto.

## NR / FG dual path

The full-resolution NVOF pass keeps its raw vector field in addition to its reliability-corrected candidate.

- NR-safe motion starts from raw NVOF, moves only a bounded amount toward the reliable/spatial candidate, and reduces confidence where reprojection is unsafe. Pre-NR temporal denoise, motion-compensated depth stabilization, DLSSNR temporal motion and optional post-NR output stabilization use this branch.
- FG motion uses the stronger repaired/spatial candidate, then applies depth-aware re-repair and the alpha3 3F/5F trailing temporal consensus before DLSS-G.

External EXR Motion remains renderer-supplied guidance and bypasses NVOF rewriting.

## UI

The existing single control is reused:

- Off - Raw NVOF
- Auto - Spatial + NR Safe / FG 3F
- Strong - Spatial + NR Safe / FG 5F

NR-only conversions can now use Auto/Strong. NVOF Output Cost is automatically kept on while the reliable path is active.

## Diagnostics

`video/logs/temporal-last.log` now includes:

- `spatial_consensus_fraction`
- `spatial_consensus_residual`
- `nr_safe_corrected_fraction`
- `nr_history_rejected_fraction`
- existing FG `repaired_fraction` / global motion / temporal-consensus metrics

The existing `flow_ms` performance column includes both full-resolution and half-resolution NVOF work.

## Validation state

Static/source contracts and platform-independent C++ algorithm tests are provided. Windows + RTX hardware validation is still required for the second NVOF session, quality gains, GPU cost and long-video lifecycle behavior.
