from pathlib import Path

root = Path(__file__).resolve().parents[1]
src = (root / 'src/video/VideoGuiApp.cpp').read_text(encoding='utf-8')

required = [
    'BeginDeferWindowPos',
    'DeferWindowPos(batch,ch,nullptr',
    'EndDeferWindowPos(batch)',
    'SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE',
    'const int deltaY=old-next;',
    'if(id==IDC_PREVIEW_ORIGINAL || id==IDC_PREVIEW_OUTPUT || id==IDC_PREVIEW_DEPTH || id==IDC_PREVIEW_MOTION || id==IDC_SIDEBAR_SCROLL)',
    'SetScrollInfo(bar,SB_CTL,&si,TRUE);',
    'RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW',
]
for needle in required:
    assert needle in src, f'missing stable-scroll contract: {needle}'

start = src.index('void SetSidebarScrollPosition')
end = src.index('\nvoid SyncTemporalUi', start)
body = src[start:end]

# Regression guard: wheel/thumb scrolling must not rerun the full two-pass layout.
assert 'Layout(s);' not in body
assert 'LayoutLabels(s);' not in body

# Controls must be translated without resize so combo boxes do not get their dropdown
# height reapplied on every wheel tick.
assert 'SWP_NOSIZE' in body

print('PASS: V0.6.5 scrollable sidebar stability v2 contract')
