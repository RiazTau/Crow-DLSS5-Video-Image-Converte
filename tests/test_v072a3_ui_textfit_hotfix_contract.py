from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / 'src/video/VideoGuiApp.cpp').read_text(encoding='utf-8')
exr = (root / 'src/video/ExternalRenderDataDialog.cpp').read_text(encoding='utf-8')

# Main video sidebar: reserve enough width for long labels/options and let combo
# dropdowns expand to their longest item instead of clipping text.
for needle in [
    'constexpr int SIDEBAR = 560;',
    'const int label=DpiScale(s,150)',
    'void FitComboDropWidth(HWND combo)',
    'CB_SETDROPPEDWIDTH',
    'DpiScale(s,460)',
    'm->ptMinTrackSize={DpiScale(s,1280),DpiScale(s,800)};',
]:
    assert needle in main, f'missing main UI text-fit contract: {needle}'

# EXR editor must no longer be a fixed 810x670 / fixed-pixel dialog.  It has its
# own DPI font, a canonical resize layout, and supports monitor-DPI changes.
for needle in [
    'UINT dpi = 96;',
    'HFONT uiFont = nullptr;',
    'void ApplyDialogFont(DialogState* s)',
    'void LayoutDialog(DialogState* s)',
    'case WM_SIZE:',
    'case WM_DPICHANGED:',
    'case WM_GETMINMAXINFO:',
    'WS_THICKFRAME | WS_MAXIMIZEBOX',
    'SS_PATHELLIPSIS',
    'CB_SETDROPPEDWIDTH',
    'ButtonWidth(s, L"Auto Calibrate"',
]:
    assert needle in exr, f'missing EXR text-fit/DPI contract: {needle}'

assert 'CW_USEDEFAULT, CW_USEDEFAULT, 810, 670' not in exr
assert 'static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT))' in exr  # fallback only
assert 'MoveWindow(ctl' in exr

print('PASS: V0.7.2 alpha3 UI text-fit / EXR DPI hotfix contract')
