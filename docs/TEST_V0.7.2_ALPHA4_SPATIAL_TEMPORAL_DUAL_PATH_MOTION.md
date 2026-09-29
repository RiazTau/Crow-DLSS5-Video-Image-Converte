# V0.7.2-alpha4 Spatial-Temporal Dual-Path Motion — RTX Test Plan

## Build / launch

Build with the normal Full Build path and confirm the main title is:

`Crow - DLSS Rendering Tool V0.7.2-alpha4`

Use the same short source clip for all A/B/C comparisons.

## Test A — NR-only dual path

- Enable DLSS5 NR.
- Disable FG.
- Select NVIDIA Optical Flow.
- Compare Off / Auto / Strong.
- Check fine repeated texture, moving text, cloth, fences and window grids for reduced temporal smear/ghosting.
- Confirm Auto/Strong remain selectable when FG is off.

Review `temporal-last.log`:

- `spatial_consensus_fraction`
- `spatial_consensus_residual`
- `nr_safe_corrected_fraction`
- `nr_history_rejected_fraction`
- denoise history/reject columns

Expected: difficult regions may show NR correction/rejection while normal static detail remains stable.

## Test B — FG-only spatial + temporal hybrid

- Disable NR.
- Enable FG 2X on RTX 40 (or supported multiplier on RTX 50).
- Select NVIDIA Optical Flow.
- Compare Off / Auto / Strong on the same fence/blind/window sequence used for alpha2/alpha3.

Expected: spatial prior should reduce wrong-phase vectors before the alpha3 temporal pass; Strong should normally correct more than Auto.

## Test C — NR -> FG

Enable both NR and FG. Verify that:

- NR does not inherit the full aggressive FG correction;
- FG tearing remains no worse than alpha3;
- NR temporal stability is improved or unchanged;
- scene cuts do not carry either coarse or temporal history into the next shot.

## Test D — External EXR regression

Select External EXR Motion. The spatial NVOF and NVOF repair paths must be bypassed; renderer motion should remain unchanged.

## Test E — performance / long-run

Compare alpha3 and alpha4 `flow_ms` on 1080p and 4K. Alpha4 intentionally runs a second half-resolution NVOF pass, so flow cost should rise. Confirm:

- no progressive VRAM growth;
- cancel is safe;
- conversion completion is safe;
- long sequences do not lose the coarse NVOF session state;
- output CFR/audio duration remains unchanged.
