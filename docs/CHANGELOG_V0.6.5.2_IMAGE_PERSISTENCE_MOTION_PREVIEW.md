# V0.6.5.2 — Image Parameter Persistence + Motion Preview

V0.6.5.2 is a focused UI/diagnostic upgrade over V0.6.5.1 External Data Auto Calibration. It does not change the direct DLSSNR Feature 18 runtime path, D3D12 batching, RuntimeCompat, Auto Depth, temporal denoising, optical-flow estimation, external-data calibration or encoder defaults.

## Image converter parameter persistence

The image converter now mirrors the video converter's explicit settings model:

- `Save Parameters` writes processing settings to `dist/image/image-parameters.ini`.
- Saved settings are loaded automatically on the next launch.
- Every persisted image-processing parameter has its own adjacent `Reset` button.
- There is no Reset All action.
- Reset changes only the current UI value; press `Save Parameters` to persist the restored factory value.
- Source/depth file paths, EXR layer/channel selections and runtime DLL selection are deliberately not persisted because they are input/runtime-specific.

Persisted controls include import exposure/tone-map/sRGB, depth mode/normalize/inverse/scale/offset, Neural Uplift, NR preset/style/intensity/local-tone/local-structure/skin-structure, Auto Skin Mask, UI Correction and iterations.

## Motion-vector live preview

The video preview region is now a fixed 2×2 matrix:

```text
Original       | DLSS5 Output
---------------+----------------
Depth Guidance | Motion Vectors
```

The Motion pane visualizes the final calibrated internal current-to-previous motion field. Hue represents vector direction and brightness represents magnitude. The main video `MV Scale X/Y` values are applied to the visualization so it reflects the effective motion supplied to DLSSNR. A robust P95 magnitude is used internally so isolated extreme vectors do not make normal motion nearly black.

The motion diagnostic image is generated at a bounded preview resolution (maximum width 960) instead of copying another full-resolution RGBA image every preview tick.

On the first frame, scene-cut reset frames, or Legacy temporal mode, no temporal motion field is used and the Motion pane shows a waiting message.

## Scroll stability retained

All four preview panes are excluded from the scrollable sidebar child translation. The V0.6.5 scroll Stability v2 behavior remains unchanged for the parameter panel.
