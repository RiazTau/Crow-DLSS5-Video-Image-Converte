# V0.7.2-alpha6 Test Plan

## Build validation

1. Run `BUILD.bat` -> mainland or global unified build.
2. Confirm CMake reports NVAPI preset support correctly when a valid SDK is present.
3. Confirm normal CMake warnings do not abort PowerShell when `cmake.exe` returns exit code 0.
4. Confirm `Crow-DLSS-Rendering-Tool.exe` title reports V0.7.2-alpha6.

## Algorithm A/B

Use the same source, same NR / FG / encoder settings, and compare alpha5 vs alpha6.

### Periodic textures

Recommended material: fences, blinds, rails, dense windows, mesh, thin repeating architecture.

Check:
- high-NVOF-confidence but high-uncertainty phase locks are still eligible for repair;
- fewer wrong-phase jumps;
- fewer local vectors that snap by one repeated period;
- Strong should improve more than Auto because it owns the 1/4-resolution prior.

### Foreground/background silhouettes

Recommended material: walking person / vehicle crossing a panning background.

Check:
- less halfway-vector smearing at object boundaries;
- lower FG tearing / dragging where opposite motion modes meet;
- no obvious foreground motion being forced to camera motion.

### Pan + zoom / rotation

Check:
- low-confidence regions follow the local affine camera field rather than one constant translation;
- corners should no longer inherit the center's translation when zoom/rotation is present.

### Acceleration

Use accelerating camera/object movement.

Check:
- temporal consensus does not flatten true acceleration;
- isolated phase slips remain corrected toward the trajectory prediction.

## Logs

Keep `video/logs/temporal-last.log` and `performance-last.log` for alpha5 and alpha6. Compare spatial consensus correction fraction/residual, temporal consensus fraction/residual, history rejection and total flow / consensus timing.

## Performance expectation

Auto should remain near alpha5 NVOF session count. Strong intentionally adds a 1/4-resolution NVOF session, so `flow_ms` can increase. Quality gain should be judged against this explicit Strong-mode cost.
