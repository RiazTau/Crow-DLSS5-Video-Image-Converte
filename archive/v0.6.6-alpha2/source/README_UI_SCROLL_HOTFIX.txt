Crow-DLSS5-Video-Image-Converter V0.6.5 - Scrollable Sidebar Stability Hotfix v2
=====================================================================

Purpose
-------
V0.6.5 added a vertically scrollable video-parameter sidebar. The first scrolling
implementation reran the full control layout and a separate label layout on every
wheel/thumb update. On Windows, repeated moves/resizes of Win32 combo boxes and
separate label/control repaint passes could leave stale paint, visual drift, overlap,
or partial clipping after several scroll operations.

V2 behavior
-----------
- Keeps the independent vertical sidebar scrollbar and mouse-wheel forwarding.
- A scroll update now translates all sidebar child windows by exactly one common delta.
- Uses one DeferWindowPos batch so labels, Reset buttons, edits, buttons and combos move atomically.
- Uses SWP_NOSIZE while scrolling; combo-box dropdown heights are not reapplied on every wheel tick.
- Invalidates/repaints the complete sidebar region after the atomic move to erase stale paint.
- WM_SIZE still rebuilds all controls from canonical logical coordinates and current scroll offset.
- Original / Output / Depth / Motion preview panes and the scrollbar itself remain fixed.

Apply
-----
Extract this hotfix directly into the current V0.6.5 project root (including the
previous MSVC build hotfix / scroll hotfix) and allow overwrite, then run
AUTO_BUILD_CN.bat again.

Scope
-----
No DLSSNR, D3D12 batching, RuntimeCompat, External EXR reader, temporal denoise,
Auto Depth, FFmpeg, encoder, saved parameters, or processing defaults are changed.
