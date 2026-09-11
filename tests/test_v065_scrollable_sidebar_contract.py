from pathlib import Path

root = Path(__file__).resolve().parents[1]
src = (root / 'src/video/VideoGuiApp.cpp').read_text(encoding='utf-8')

required = [
    'IDC_SIDEBAR_SCROLL',
    'int sidebarScrollY = 0;',
    'int sidebarContentHeight = 0;',
    'Make(h,L"SCROLLBAR",L"",SBS_VERT,IDC_SIDEBAR_SCROLL);',
    'case WM_VSCROLL:',
    'case WM_MOUSEWHEEL:',
    'SetSidebarScrollPosition(s,next);',
    'SetWindowSubclass(ch,SidebarChildSubclassProc',
    'y-scrollOffset',
    'scrollInfo.fMask=SIF_RANGE|SIF_PAGE|SIF_POS;',
    'ShowWindow(sidebarScroll,maxScroll>0?SW_SHOW:SW_HIDE);',
    'm->ptMinTrackSize={1180,760}',
]
for needle in required:
    assert needle in src, f'missing scrollable-sidebar contract: {needle}'

# The old fixed-height layout clipped the bottom of the sidebar by sizing status only
# from the remaining visible client height. The scrolling layout must keep a logical
# content height instead.
assert 'move(IDC_STATUS,x,y,w,std::max(44,H-y-10))' not in src
assert 's->sidebarContentHeight=y;' in src

# Protect the MSVC hotfix while touching Layout again.
assert 'ww=resetW' not in src
assert 'hh=row' not in src

print('PASS: V0.6.5 scrollable sidebar contract')
