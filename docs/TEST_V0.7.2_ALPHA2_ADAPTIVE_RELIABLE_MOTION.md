# V0.7.2-alpha2 Adaptive Reliable Motion — RTX Test Plan

## 1. Baseline regression
Use the same short SDR test clip that previously passed V0.7.2-alpha1.

Test:
- NR OFF
- FG ON
- 2X
- NVIDIA Optical Flow
- FG Motion Stabilization OFF

Expected: behavior should match the previous raw NVOF FG path.

## 2. Auto mode
Repeat with:
- FG Motion Stabilization = Auto
- NVOF Quality = Slow
- NVOF Output Cost = ON

Expected:
- conversion completes normally;
- no cancel/finish crash;
- `temporal-last.log` contains nonzero confidence and, on difficult material, a nonzero `repaired_fraction`.

## 3. Parallel / repetitive texture stress clip
Recommended material:
- fences;
- window grids;
- venetian blinds;
- railings;
- brick/window repetition during camera pan;
- dense horizontal/vertical stripes.

Compare OFF / AUTO / STRONG from identical source frames.

Expected target:
- Auto reduces local phase jumps / line displacement compared with Off;
- Strong repairs more pixels than Auto and is intended only when Auto is insufficient;
- confident moving-object boundaries should not become globally smeared.

## 4. Depth-aware repair
Test the same clip with:
1. Zero Depth;
2. Auto Depth;
3. External EXR Depth if available.

Expected:
- Auto/External depth should reduce motion propagation across foreground/background boundaries;
- no inversion-dependent catastrophic motion displacement.

## 5. External EXR Motion regression
Set Temporal Motion to External EXR Motion.

Expected:
- FG Motion Stabilization control is not used to rewrite the EXR vectors;
- output matches the renderer-provided motion path.

## 6. MFG regression
On hardware that supports it, repeat Auto mode at available 3X-6X multipliers.

Expected:
- every generated subframe uses the same sanitized guidance field for the real-frame interval;
- output FPS and CFR duration remain correct.

## 7. Performance comparison
Compare `performance-last.csv` / conversion wall time for:
- Off
- Auto
- Strong

Expected:
- Off has no backward-flow overhead;
- Auto/Strong cost more than Off because NVOF generates backward flow and the CPU reliability repair pass runs;
- no unexpected multi-second stalls per frame.

## 8. Logs to provide on failure
Please provide:
- `video/logs/temporal-last.log`
- `video/logs/performance-last.csv`
- `video/logs/decoder-last.log`
- `video/logs/encoder-last.log`
- screenshot or short crop showing the affected repeating texture
- selected NVOF grid / quality / stabilization mode / depth mode
