# V0.3.2 Change Log

## Preview / maximize fix

- Reworked the preview-region layout so maximized/restored windows keep both child preview windows inside the main client area.
- Added `WS_CLIPSIBLINGS` and explicit GDI clipping to prevent preview panes from painting over each other.
- Main and Depth preview painters now render through a compatible memory bitmap before blitting to screen, reducing maximize/resize flicker and stale-frame artefacts.
- Preview child classes now request redraw on horizontal/vertical resize.
- Main window now has a minimum tracking size and ignores minimized-size layout passes.

## Interactive zoom

- Mouse wheel: zoom 1x Fit .. 20x, centered at the cursor.
- Left drag: pan while zoomed.
- Double-click: reset to Fit.
- Original and DLSS5 views stay synchronized for same-region A/B inspection.
- Depth Guidance has its own independent zoom/pan state.
- Pan is reclamped after every child-window resize.

No DLSSNR Feature-18 parameter mapping, EXR import, depth generation, or runtime logic was changed.
