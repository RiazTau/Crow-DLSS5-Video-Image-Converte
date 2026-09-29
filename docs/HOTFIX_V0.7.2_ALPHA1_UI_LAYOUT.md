# V0.7.2-alpha1 UI Layout Hotfix 2

## Problem

Maximizing the video converter or moving it between monitors with different Windows DPI scaling could make parameter labels, fields, combo boxes and Reset buttons appear misaligned.

## Root causes

1. Parameter controls were laid out in `Layout()` while labels were maintained by a second, manually duplicated `LayoutLabels()` coordinate sequence. Any new control or spacing change could desynchronize the two passes on `WM_SIZE`.
2. The video UI used fixed physical-pixel metrics and had no Per-Monitor DPI awareness / `WM_DPICHANGED` handling.
3. Scroll and minimum-window metrics also used unscaled pixel constants.

## Fix

- Removed the duplicated `LayoutLabels()` pass. Labels, fields and Reset buttons now share one canonical layout pass.
- Added Per-Monitor DPI Awareness V2 initialization when available.
- Added `WM_DPICHANGED` handling using the Windows suggested window rectangle.
- Added DPI-scaled sidebar, scroll, preview, minimum-size and wheel-step metrics.
- Rebuilds a DPI-appropriate Segoe UI font when DPI changes.
- Keeps the existing atomic scroll translation path; wheel/thumb scrolling still does not rerun the full layout.

No NR/FG/MFG, EXR, NVOF, encoder or runtime processing behavior is changed.
