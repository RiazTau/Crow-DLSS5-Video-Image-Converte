# V0.7.2-alpha1 — MFG / External Guidance / FG Model Preset

## Summary

V0.7.2-alpha1 expands the unified V0.7.1 NR + FG pipeline without adding unrelated FG tuning controls.

### Added

- FG Multiplier UI and backend support for 2X-6X.
- RTX 40-series policy + backend clamp to 2X.
- RTX 50-series capability-driven MFG, capped by Crow at 6X and by the runtime-reported `MultiFrameCountMax`.
- MFG-aware output FPS, output-frame totals, preview sequence, scene-cut fallback slots and final CFR duration padding.
- External EXR Motion as a valid FG motion-guidance source.
- External EXR Depth and Auto Depth as valid FG depth-guidance sources.
- Shared NR/FG EXR configuration through the existing External Render Data reader/dialog.
- FG Model Preset selector using NVIDIA NVAPI Driver Settings.
- Build-time NVIDIA NVAPI SDK dependency.

### FG Model Preset choices

- Driver Default
- Preset A
- Preset B
- Latest

Driver Default removes Crow's own FG override instead of forcing a model. Other selections apply the public NVIDIA DLSS-FG driver setting to the driver profile that owns the Crow executable.

### Intentionally not exposed

V0.7.2-alpha1 does not add low-value FG camera/matrix/near/far/depth-separation controls. Guidance interpretation remains automatic except for the existing EXR motion/depth settings already used by NR.

## Hardware validation status

The source passes static/contract regression tests. Standalone 2X FG was already validated on RTX hardware in V0.7.0-alpha1. V0.7.2-alpha1 MFG above 2X requires RTX 50-series hardware validation.
