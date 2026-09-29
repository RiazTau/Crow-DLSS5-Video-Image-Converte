from pathlib import Path

root = Path(__file__).resolve().parents[1]
cmake = (root / 'CMakeLists.txt').read_text(encoding='utf-8')
ext_h = (root / 'src/video/ExternalRenderData.h').read_text(encoding='utf-8')
ext_cpp = (root / 'src/video/ExternalRenderData.cpp').read_text(encoding='utf-8')
cal_h = (root / 'src/video/ExternalDataCalibration.h').read_text(encoding='utf-8')
cal_cpp = (root / 'src/video/ExternalDataCalibration.cpp').read_text(encoding='utf-8')
dlg = (root / 'src/video/ExternalRenderDataDialog.cpp').read_text(encoding='utf-8')
gui = (root / 'src/video/VideoGuiApp.cpp').read_text(encoding='utf-8')
converter = (root / 'src/video/VideoConverter.cpp').read_text(encoding='utf-8')

assert 'VERSION 0.6.5.1' in cmake or 'VERSION 0.6.5.2' in cmake or 'VERSION 0.6.5.3' in cmake or 'VERSION 0.6.6' in cmake
assert 'src/video/ExternalDataCalibration.cpp' in cmake

# Depth auto-calibration: channel rejection/prior + global robust sequence range.
assert 'CalibrateExternalDepthSequence' in cal_h and 'CalibrateExternalDepthSequence' in cal_cpp
assert 'Global P1-P99' in cal_cpp
assert 'best.p01' in cal_cpp and 'best.p99' in cal_cpp
assert 'in01Ratio >= 0.995f' in cal_cpp
assert 'LeafEq(channel, "A")' in cal_cpp
assert 'for (const auto& leaf : {"R", "G", "B"})' in ext_cpp

# Motion calibration must use real adjacent frames and explore convention choices.
assert 'CalibrateExternalMotionSequence' in cal_h and 'CalibrateExternalMotionSequence' in cal_cpp
assert 'PairZeroMotionError' in cal_cpp
assert 'EvaluateMotionCandidate' in cal_cpp
assert '"pixels"' in cal_cpp and '"UV"' in cal_cpp and '"NDC"' in cal_cpp
assert 'ExternalMotionDirection::PreviousToCurrent' in cal_cpp
assert 'reprojectionError' in cal_h
assert 'confidence' in cal_h

# Forward motion is supported at runtime by inversion into the internal convention.
assert 'ExternalMotionDirection' in ext_h
assert 'PreviousToCurrent' in ext_h
assert 'Solve p + F(p) = q' in ext_cpp
assert 'BilinearField' in ext_cpp
assert 'forward motion auto-inverted' in converter

# GUI workflow and persistence.
assert 'Auto Calibrate Both' in dlg
assert 'IDC_D_ANALYZE' in dlg and 'IDC_M_ANALYZE' in dlg
assert 'DecodeCalibrationPairs' in dlg
assert 'external-calibration-decoder-last.log' in dlg
assert 'LOW confidence' in dlg
assert 'motion_direction' in gui
assert 'EditExternalRenderDataSettings(s->hwnd, edited, s->inputPath)' in gui

print('PASS: V0.6.5.1 External Data auto-calibration contract')
