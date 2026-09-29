from pathlib import Path

root = Path(__file__).resolve().parents[1]
src = (root / 'src/video/VideoGuiApp.cpp').read_text(encoding='utf-8')

required = [
    'UINT dpi = 96;',
    'HFONT uiFont = nullptr;',
    'int DpiScale(const State* s, int logicalPx)',
    'void ApplyDpiFont(State* s)',
    'case WM_DPICHANGED:',
    'SetProcessDpiAwarenessContext',
    'DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2',
    'auto labelAt=[&](int parameterId,int Y)',
    'labelAt(parameterId,Y); move(parameterId,valueX,Y,standardValueW,row);',
    'm->ptMinTrackSize={DpiScale(s,1280),DpiScale(s,800)};',
]
for needle in required:
    assert needle in src, f'missing V0.7.2 UI-layout hotfix contract: {needle}'

# Labels and fields must have one canonical resize layout.  The old duplicated
# LayoutLabels() pass was the source of drift when controls were added/reordered.
assert 'void LayoutLabels(State* s)' not in src
assert 'LayoutLabels(s);' not in src

# Fixed-pixel sidebar geometry must not survive in the active layout path.
layout = src[src.index('void Layout(State* s) {'):src.index('\nvoid AddLabelAt', src.index('void Layout(State* s) {'))]
assert 'const int x=12' not in layout
assert 'const int label=112' not in layout
assert 'const int resetW=50' not in layout
assert 'DpiScale(s,150)' in layout
assert 'DpiScale(s,50)' in layout

print('PASS: V0.7.2 alpha1 fullscreen/DPI single-pass layout hotfix contract')
