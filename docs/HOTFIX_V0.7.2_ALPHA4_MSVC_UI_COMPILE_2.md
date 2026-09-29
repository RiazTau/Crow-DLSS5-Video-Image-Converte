# V0.7.2-alpha4 MSVC UI Compile Hotfix 2

## Scope

This hotfix fixes the Windows/MSVC compile failure reported after the alpha3 UI Text-Fit changes were carried into alpha4.

The failing helpers were the ComboBox drop-down width calculators in:

- `src/video/VideoGuiApp.cpp`
- `src/video/ExternalRenderDataDialog.cpp`

`RECT::right - RECT::left` is a Win32 `LONG`, while the measured text width is an `int`. MSVC does not deduce `std::max(long, int)`. The resulting parse failure also produced a cascading `SendMessageW` argument-count error.

## Fix

Both helpers now:

1. explicitly convert the current control width to `int`;
2. compute the desired width as `int`;
3. call `(std::max)(int, int)`;
4. call `::SendMessageW(combo, CB_SETDROPPEDWIDTH, ..., 0)` with the final `int` width.

No NR, FG/MFG, NVOF, Spatial Consensus, Temporal Consensus, EXR parsing, encoding, or runtime behavior is changed.

The Build Diagnostics Hotfix 1 logging improvements are retained.
