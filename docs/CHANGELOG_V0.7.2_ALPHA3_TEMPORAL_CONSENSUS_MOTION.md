# Crow - DLSS Rendering Tool V0.7.2-alpha3
## Temporal Consensus Motion

V0.7.2-alpha3 extends V0.7.2-alpha2 Adaptive Reliable Motion with a second, independent temporal-consensus stage for NVOF-guided DLSS Frame Generation.

## Why

Repeated parallel textures can create a locally plausible optical-flow phase lock. Hardware cost and forward/backward consistency may both remain deceptively good if both directions choose the same repeated feature. Alpha3 therefore asks an additional question: does the current vector agree with the motion trajectory of the same reprojected surface over recent frames?

## Hybrid pipeline

1. NVOF Forward + Backward in one execute.
2. Forward/Backward cost + FB consistency + photometric reliability.
3. Alpha2 local/global reliable-motion repair.
4. Optional depth-aware alpha2 re-repair.
5. **Alpha3 trailing temporal trajectory consensus.**
6. Final sanitized current->previous motion is sent to DLSS-G.

External EXR Motion is treated as renderer guidance and bypasses steps 1-5.

## Auto

`Auto - Reliable + 3F Consensus`

- compact short history;
- conservative temporal override;
- primarily rescues sudden low/moderate-confidence phase slips;
- high-current-confidence vectors receive extra protection.

## Strong

`Strong - Up to 5F Consensus`

- up to three historical flow fields plus the current interval;
- finer compact history lattice;
- stronger rejection of temporally isolated phase slips;
- smooth-acceleration predictor prevents a legitimate accelerating object from being flattened to constant velocity;
- optional depth history prevents trajectory votes from crossing foreground/background layers.

## Memory design

Alpha3 does not retain several full-resolution 4K flow/depth frames. Historical motion, confidence and optional depth are stored on a compact lattice derived from the selected NVOF grid. Corrections are then interpolated back as a low-frequency trajectory delta over the detailed alpha2 flow field.

## Diagnostics

`video/logs/temporal-last.log` adds:

- `temporal_consensus_fraction`
- `temporal_consensus_residual`
- `temporal_consensus_history`

`video/logs/performance-last.csv` adds:

- `motion_consensus_ms`

## Compatibility

- FG/MFG 2X-6X behavior is unchanged.
- RTX 40 remains capped to 2X.
- External EXR Motion/Depth and Auto Depth remain supported.
- FG Preset / NVAPI behavior remains unchanged.
- Scene cuts immediately clear temporal-consensus history.
- `Off - Raw NVOF` remains the no-repair baseline and does not create the consensus stage.
