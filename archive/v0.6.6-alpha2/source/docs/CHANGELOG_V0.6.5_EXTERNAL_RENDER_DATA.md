# V0.6.5 External Render Data

V0.6.5 is built directly on V0.6.4 and keeps the V0.6.3 performance path, V0.6.4 anti-warp defaults, saved parameters, RTX40/RTX50 runtime compatibility, AV1 resilience, Full HQ denoise, DIS flow and Auto Depth.

## External EXR depth sequences

The video converter can now use an EXR sequence as DLSSNR depth guidance. Select the first numbered frame (for example `shot_1001.exr`); V0.6.5 increments the final numeric run for every decoded video frame. The selected exact EXR channel is read as FLOAT/HALF through TinyEXR.

Depth mapping supports:

- `Fixed Near/Far`: maps render-space Z through one fixed range shared by the whole sequence. This avoids per-frame normalization pumping.
- `Raw 0..1`: preserves a pre-normalized depth pass, with clamping only.
- `DepthInverted` remains explicit so near-white and near-black conventions are both supported.

External depth is strict-resolution in V0.6.5. A render-data frame whose dimensions differ from the decoded video is rejected instead of silently resampled.

## External EXR motion sequences

Temporal mode adds `External EXR Motion - CG Ground Truth`. The imported X/Y channels must represent **current -> previous** motion in full-resolution pixel units before optional import scale/flip controls are applied.

The same imported motion field is used by:

- Full HQ / Temporal HQ history reprojection;
- optional depth stabilization;
- DLSSNR Feature 18 motion input;
- optional post-NR output stabilization.

A light photometric confidence gate is generated from the external vector reprojection to reject disocclusion boundaries and obvious channel/sign mapping errors. Scene-cut detection remains active without invoking DIS optical flow.

## Blender Multilayer EXR workflow

The External Render Data editor enumerates exact EXR channels and auto-detects common Blender-style names such as `*.Depth.Z`, `*.Vector.X` and `*.Vector.Y`. Blender's Vector pass is four-component XYZW data; V0.6.5 deliberately keeps the exact selected channels, scale and flip controls visible instead of hiding channel convention assumptions.

The first selected EXR frame defines sequence numbering, including starts such as 1001. Depth and motion may point at the same multilayer EXR sequence or separate EXR sequences.

## Parameter persistence

`Save Parameters` continues to persist processing parameters. V0.6.5 also stores external channel/mapping/scale/flip settings under `[ExternalRenderData]`, but deliberately does **not** save external EXR paths because they are input-media paths. The External Render Data editor provides independent Reset controls for channel, numerical and convention parameters, plus Auto channel detection for file-dependent channel selections.

## Safety / fallback

- Missing sequence frames fail with the exact missing EXR path; no previous-frame substitution is performed.
- External depth and motion are optional independently.
- Existing Zero/Auto Depth, DIS optical flow and CPU flow modes remain unchanged.
- Runtime Self-Test remains isolated from the V0.6.3 performance experiment as before.

## Current EXR limits

The exact auxiliary-channel loader currently accepts single-part scanline EXR. Tiled and multipart EXR are rejected explicitly rather than being misread. This inherits the conservative TinyEXR path used by the image converter and remains a target for later expansion after Blender real-file testing.

## Scrollable parameter sidebar hotfix — 2026-09-03

- Added an independent vertical scrollbar to the main video-converter sidebar.
- Mouse wheel over ordinary sidebar controls, including combo/edit/button children, is forwarded to the sidebar scroller instead of requiring the user to drag the thumb.
- Label positions and all per-parameter Reset buttons share the same scroll offset.
- Preview panes remain fixed and are not part of the scroll surface.
- Replaced the old bottom-status `H-y` sizing dependency with a logical sidebar content height, preventing Start/Cancel/progress/status controls from being clipped.
- Scroll range is recomputed on resize and the thumb is hidden when all sidebar content fits.
- Minimum tracked window height is now 760 px because the sidebar no longer needs to fit vertically in one viewport.
- No processing/runtime/EXR behavior was changed.

## Scrollable sidebar stability hotfix v2 — 2026-09-03

- Fixed repeated scrolling causing labels, buttons and combo-box fields to visually drift, overlap or remain partially clipped.
- Wheel/scroll-thumb updates no longer rerun the complete sidebar `Layout()` plus a separate label pass on every tick.
- All scrollable sidebar child windows are translated by the same delta in one `DeferWindowPos` batch; combo boxes are moved without being resized during scrolling.
- The sidebar region is invalidated after each atomic move so old child-window paint is erased instead of accumulating visual remnants.
- `WM_SIZE` still rebuilds every control from canonical logical coordinates, so resize remains deterministic and cannot accumulate scroll-coordinate error.
- Preview panes and the vertical scrollbar stay fixed.
- No processing, parameter, EXR, temporal, DLSSNR, D3D12, runtime or encoder behavior changed.
