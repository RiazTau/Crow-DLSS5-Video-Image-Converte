# Crow - DLSS Rendering Tool V0.7.2-alpha2
## Adaptive Reliable Motion

### Goal
Reduce DLSS Frame Generation spatial displacement artifacts on repetitive/parallel textures (fences, blinds, rails, dense windows, repeated stripes) by repairing unreliable NVOF guidance before it reaches DLSS-G.

### NVOF backend
- Uses `NV_OF_PRED_DIRECTION_BOTH` when FG Motion Stabilization is Auto/Strong.
- Receives forward current->previous flow and backward previous->current flow from one NVOF execute call.
- Reads forward and backward 8-bit NVOF cost buffers when cost output is enabled.
- `Off` restores the previous forward-only NVOF path and avoids backward-flow overhead.

### Reliability model
Crow combines:
- forward NVOF cost;
- backward NVOF cost sampled at the reprojected point;
- forward/backward consistency error;
- photometric reprojection residual.

The result is retained as the per-pixel confidence field.

### Motion repair
For low-confidence motion only:
1. gather high-confidence nearby motion;
2. reject neighbours crossing a depth discontinuity when depth is available;
3. blend toward local reliable motion;
4. if local consensus is unavailable, fall back to a robust median global motion estimate.

High-confidence vectors are preserved rather than globally smoothed.

### Depth-aware second pass
When Auto Depth or External EXR Depth is available, the unified video pipeline performs a second FG-only reliability refinement after depth stabilization. This prevents repaired vectors from propagating across foreground/background boundaries.

### UI
New control:
- `FG Motion Stabilization = Off - Raw NVOF`
- `FG Motion Stabilization = Auto - Reliable Motion` (default)
- `FG Motion Stabilization = Strong - Periodic Texture`

The control is available only when FG/MFG uses NVIDIA Optical Flow. External EXR Motion is treated as renderer-supplied guidance and is not modified.

### Diagnostics
`video/logs/temporal-last.log` adds:
- `repaired_fraction`
- `global_mv_x`
- `global_mv_y`

These values help determine whether an artifact region was being rejected/repaired and whether a global fallback was active.

### Compatibility
- NR-only keeps Adaptive Reliable Motion disabled.
- FG Motion Stabilization Off uses forward-only NVOF.
- Existing MFG, External EXR, FG Model Preset, Performance Mode, Portable layout and fullscreen/DPI UI hotfix behavior remain intact.
