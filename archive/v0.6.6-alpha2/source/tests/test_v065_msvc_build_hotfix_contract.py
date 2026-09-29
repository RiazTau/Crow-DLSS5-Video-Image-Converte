from pathlib import Path

root = Path(__file__).resolve().parents[1]
gui = (root / 'src/video/VideoGuiApp.cpp').read_text(encoding='utf-8')
conv = (root / 'src/video/VideoConverter.cpp').read_text(encoding='utf-8')

# MSVC C2587: local automatic variables cannot be referenced by lambda default arguments.
assert 'int ww=resetW' not in gui
assert 'int hh=row' not in gui
assert 'auto reset=[&](int parameterId,int X,int Y)' in gui
assert 'auto standard=[&](int parameterId,int Y)' in gui
assert 'auto standardH=[&](int parameterId,int Y,int hh)' in gui

# MSVC C4129: Windows path backslashes in string literals must be escaped.
assert 'video\\\\logs\\\\performance-summary-last.txt' in conv
assert 'video\\logs\\performance-summary-last.txt' not in conv.replace('video\\\\logs\\\\performance-summary-last.txt', '')

print('PASS: V0.6.5 MSVC build hotfix contract')
