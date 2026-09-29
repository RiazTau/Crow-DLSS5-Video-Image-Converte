# V0.7.1-alpha1 — Unified NR + FG 2X

## Purpose

This alpha merges the previously separate DLSS5 Neural Rendering video path and the hardware-validated standalone DLSS Frame Generation 2X path into one main video application and one conversion task.

## Main changes

### Unified main executable

The primary video application is now `Crow-DLSS-Rendering-Tool.exe`.

Two independent UI feature switches select the processing plan:

- DLSS5 Neural Rendering (NR)
- DLSS Frame Generation 2X

When both are enabled, the fixed alpha pipeline order is `NR -> FG 2X`.

### Single source motion pass

The combined path calculates NVIDIA Optical Flow once per source-frame pair and shares its motion/reset contract across temporal processing. FG forces the validated NVOF temporal backend in this alpha.

### Shared NGX Core session

A new `NgxCoreSession` owns NGX Core initialization/shutdown only when both NR and FG coexist. NR and FG retain their feature-specific create/evaluate/release logic. Single-feature modes preserve the previous runner-owned core lifetime.

### Final-output sequencer

Encoding and preview consume the same final output order. With FG 2X enabled this is conceptually:

`REAL0 -> FG0.5 -> REAL1 -> FG1.5 -> REAL2 ...`

At scene cuts or when interpolation is disabled, a duration-preserving `FALLBACK` frame is emitted rather than shortening the CFR timeline.

### Final-output preview and FPS overlay

The output pane is renamed `FINAL OUTPUT - LIVE` and previews generated frames as well as real frames. The overlay reports source FPS, output FPS, preview FPS, output-frame index, and `REAL / FG / FALLBACK` frame type.

### Directory/build cleanup retained

The source root exposes only `BUILD.bat` as the user build entry. `dist` top-level is reserved for main application EXEs, while diagnostics remain in `dist/tools`.

### Diagnostic FG retained

The V0.7.0-alpha1 standalone FG implementation is still built as `dist/tools/Crow-DLSS-Rendering-Tool-FG-Diagnostic.exe` for A/B diagnosis and rollback testing.

## Deliberately deferred

The following are not part of this alpha:

- NR depth reuse as FG depth guidance
- 3X/4X/5X/6X MFG
- GPU-resident zero-copy NR -> FG handoff
- NVDEC -> D3D12 -> NVENC all-GPU pipeline
- Ray Reconstruction / Super Resolution integration

## Validation status

The prior standalone 2X FG path has passed user RTX hardware testing. V0.7.1-alpha1 combined NR+FG and shared-NGX-lifetime behavior is new and must be validated on the target Windows/RTX machine before it is treated as stable.
