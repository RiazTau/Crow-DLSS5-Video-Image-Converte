from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
cmake = (root / 'CMakeLists.txt').read_text(encoding='utf-8')
image_gui = (root / 'src/gui/GuiApp.cpp').read_text(encoding='utf-8')
video_gui = (root / 'src/video/VideoGuiApp.cpp').read_text(encoding='utf-8')
video_h = (root / 'src/video/VideoConverter.h').read_text(encoding='utf-8')
video_cpp = (root / 'src/video/VideoConverter.cpp').read_text(encoding='utf-8')
motion_h = (root / 'src/video/MotionPreview.h').read_text(encoding='utf-8')
motion_cpp = (root / 'src/video/MotionPreview.cpp').read_text(encoding='utf-8')

assert 'VERSION 0.6.5.2' in cmake or 'VERSION 0.6.5.3' in cmake or 'VERSION 0.6.6' in cmake
assert 'src/video/MotionPreview.cpp' in cmake

# Image converter now mirrors the video's explicit parameter persistence + per-control reset model.
assert 'kImageParameterSpecs' in image_gui
assert 'IDC_SAVE_PARAMETERS' in image_gui
assert 'L"Save Parameters"' in image_gui
assert 'image-parameters.ini' in image_gui
assert 'ImageParameters' in image_gui
assert 'SaveImageParameters(s)' in image_gui
assert 'LoadSavedImageParameters(s)' in image_gui
assert 'ResetOneImageParameter' in image_gui
assert 'ImageResetButtonId' in image_gui
assert 'Reset All' not in image_gui
block = image_gui.split('static constexpr ImageParameterSpec kImageParameterSpecs[] = {', 1)[1].split('};', 1)[0]
ids = re.findall(r'\{(IDC_[A-Z0-9_]+),\s*ImageParameterKind::', block)
assert len(ids) == 18, ids
assert len(set(ids)) == 18
assert 'Source/depth file paths, EXR layer/channel selections and runtime DLL are input-specific and are not persisted.' in image_gui

# Four fixed preview panes form a true 2x2 matrix and are excluded from sidebar scrolling.
assert 'IDC_PREVIEW_MOTION' in video_gui
assert 'previewMotion' in video_gui
assert 'previewCtx[4]' in video_gui
assert 'MOTION VECTORS - LIVE | HSV direction / magnitude' in video_gui
assert 'MoveWindow(s->previewDepth,px,bottomY,leftW,bottomH,TRUE);' in video_gui
assert 'MoveWindow(s->previewMotion,px+leftW+gapPreview,bottomY,rightW,bottomH,TRUE);' in video_gui
assert 'IDC_PREVIEW_MOTION || id==IDC_SIDEBAR_SCROLL' in video_gui

# Preview receives the same calibrated internal motion field used by the processing path,
# plus the effective DLSS MV X/Y scale without copying a full-resolution RGBA motion image.
assert 'const std::vector<float>*, const std::vector<float>*' in video_h
assert 'settings.mvecScaleX, settings.mvecScaleY' in video_cpp
assert 'BuildMotionPreview(*motion, a.width, a.height, motionScaleX, motionScaleY, 960)' in video_gui
assert 'robustMagnitudePixels' in motion_h and 'RobustMagnitude' in motion_cpp
assert 'std::atan2(vy, vx)' in motion_cpp
assert 'Hue encodes direction' in motion_h

print('PASS: V0.6.5.2 image persistence/reset + 2x2 motion preview contract')
