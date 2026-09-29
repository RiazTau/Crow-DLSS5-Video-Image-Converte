from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
main = (ROOT / 'src/video/VideoGuiApp.cpp').read_text(encoding='utf-8')
exr = (ROOT / 'src/video/ExternalRenderDataDialog.cpp').read_text(encoding='utf-8')

# MSVC cannot deduce std::max(long,int) for RECT dimensions.  The text-fit
# helper must normalize RECT width to int before calculating CB_SETDROPPEDWIDTH.
for src in (main, exr):
    assert 'const int currentWidth = static_cast<int>(rc.right - rc.left);' in src
    assert 'const int dropWidth = (std::max)(currentWidth, desiredWidth);' in src
    assert '::SendMessageW(combo, CB_SETDROPPEDWIDTH, static_cast<WPARAM>(dropWidth), 0);' in src
    assert 'std::max(rc.right - rc.left' not in src

print('PASS: V0.7.2-alpha4 MSVC UI text-fit compile hotfix contract')
