# V0.7.2-alpha3 UI Text-Fit Hotfix 1

This hotfix fixes clipped labels/options in the video parameter UI and, most importantly, the External EXR Render Data editor.

## Main video window

- Sidebar logical width increased from 480 to 560 px.
- Parameter-label column increased from 112 to 150 logical px.
- Minimum main-window size increased to 1280x800 logical px.
- Combo-box drop-down widths are measured from their longest item and expanded with `CB_SETDROPPEDWIDTH`.
- The existing Per-Monitor DPI V2 / single-pass layout remains intact.

## External EXR Render Data editor

The old editor used a fixed 810x670 window, fixed pixel coordinates and `DEFAULT_GUI_FONT` with no `WM_SIZE` or `WM_DPICHANGED` handling. It is now a DPI-aware resizable window.

- Segoe UI 9pt font scaled to current monitor DPI.
- Canonical `LayoutDialog()` handles all controls in one resize pass.
- `WM_SIZE`, `WM_DPICHANGED` and `WM_GETMINMAXINFO` are handled.
- Dialog can be resized/maximized (`WS_THICKFRAME | WS_MAXIMIZEBOX`).
- Paths and sequence descriptions stretch with the window; sequence text uses path ellipsis.
- Dynamic EXR channel combos and direction combos expand their drop-down width to the longest item.
- Button widths are measured from their actual text, avoiding clipped `Auto Calibrate`, `Quick Auto`, etc.
- Result/note fields wrap instead of being truncated.
- Scale and Flip controls are separated into independent rows to avoid collisions at high DPI.

This hotfix does not change NR, FG/MFG, NVOF, Temporal Consensus, EXR parsing/calibration or encoding behavior.
