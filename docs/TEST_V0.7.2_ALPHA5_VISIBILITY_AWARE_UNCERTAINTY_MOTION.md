# V0.7.2-alpha5 Visibility-Aware Uncertainty Motion — RTX Test Plan

## 1. Build validation

On the current Windows RTX test system, rebuild with the existing NVOF SDK and the refreshed NVAPI checkout. Confirm CMake prints `NVAPI FG preset support: 1`, ordinary CMake warnings do not terminate PowerShell when the process exits 0, and all main targets complete.

## 2. Static / unit validation

Run the contract tests plus `test-nvof-postprocess`, `test-spatial-motion-consensus` and `test-temporal-motion-consensus`. The postprocess test verifies explicit disocclusion at out-of-bounds history reprojection, lower uncertainty for consistent bidirectional flow, and zero NR history confidence for a revealed edge.

## 3. Visual A/B material

Use the same alpha4 stress clips plus explicit visibility cases:

- person/car crossing a high-detail background;
- newly revealed background behind a fast foreground object;
- fences, blinds, rails and repeated windows during camera pan;
- fast pan with foreground silhouette crossing;
- zoom/rotation scenes where global translation is only approximate;
- motion blur and exposure/light changes.

Compare alpha4 vs alpha5 at Auto and Strong.

## 4. Expected NR behavior

On newly revealed surfaces, alpha5 should reduce temporal history contamination and trailing/ghosted old content. Confirm `disoccluded_fraction` rises around reveal events and `nr_history_rejected_fraction` follows without causing broad full-frame history collapse.

## 5. Expected FG behavior

FG should avoid pulling disoccluded pixels through stale 3F/5F trajectories. Inspect object silhouettes for reduced tearing/phase carry-over. Local repair should still work when nearby visible vectors agree; a disoccluded target without local support should remain uncertain rather than being forced onto global motion.

## 6. Regression checks

- Static or slow clean scenes should keep high `mean_history_visibility` and low `high_uncertainty_fraction`.
- External EXR Motion must remain untouched by NVOF rewriting.
- Scene cuts must reset temporal history.
- Auto remains conservative; Strong may repair more but must not increase stale-history artifacts at reveal boundaries.
- Record `flow_ms` change versus alpha4; the new topology pass is CPU/grid based and should be materially cheaper than adding a third NVOF session.
