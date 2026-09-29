# V0.7.2-alpha3 Temporal Consensus Motion — RTX Test Plan

## 1. Build / launch regression

Build through `BUILD.bat` and confirm the main application title is exactly:

`Crow - DLSS Rendering Tool V0.7.2-alpha3`

No feature/branch suffix should appear in the window title.

## 2. Baseline A/B clip

Use the same clip that previously showed obvious displacement on fences, blinds, rails, dense windows or other repeated parallel textures. Keep every setting identical except `FG Motion Stabilization`.

Run:

1. `Off - Raw NVOF`
2. `Auto - Reliable + 3F Consensus`
3. `Strong - Up to 5F Consensus`

Compare the exact same generated-frame intervals frame-by-frame.

## 3. Expected behavior

### Off

Should reproduce the raw NVOF-guided FG baseline. `temporal_consensus_fraction` should remain zero because the consensus stage is not created.

### Auto

Should reduce isolated phase jumps without visibly flattening ordinary object acceleration. It is expected to correct fewer regions than Strong.

### Strong

Should give the largest improvement on long repeated structures. Watch for over-stabilization on hair, fingers, rotating blades, particles, water and other genuinely irregular motion.

## 4. Temporal log

Open `video/logs/temporal-last.log` and inspect:

- `repaired_fraction` — alpha2 spatial/reliability repair;
- `temporal_consensus_fraction` — alpha3 trajectory corrections;
- `temporal_consensus_residual` — average vector disagreement in corrected cells;
- `temporal_consensus_history` — available trailing history.

On the first intervals after startup or a scene cut, history should be low/zero and gradually build again.

## 5. Scene-cut reset

Use a clip containing a hard cut. The first interval of the new shot must not inherit motion from the prior shot. Verify `reset=1` around the cut and that consensus history restarts.

## 6. Acceleration regression

Use a vehicle/person/camera movement with obvious smooth acceleration. Strong should not force the motion to the previous constant velocity. If accelerated objects lag behind, capture `temporal-last.log` for that interval.

## 7. Depth boundary regression

Repeat with Auto Depth or a good External EXR Depth sequence. Inspect foreground fences/rails over a distant background. The temporal vote should not migrate background motion onto the foreground structure.

## 8. External EXR Motion

Select External EXR Motion. Alpha3 must not rewrite renderer motion. This path is a regression test only; Temporal Consensus is NVOF-specific.

## 9. Performance

Use `performance-last.csv` and compare `motion_consensus_ms` for Auto versus Strong. Strong intentionally keeps a finer/longer compact history and should normally cost more.

## 10. MFG regression

On RTX 50 hardware, repeat at available 3X-6X multipliers. On RTX 40 hardware only 2X should remain selectable. Temporal Consensus changes guidance quality, not multiplier capability.
