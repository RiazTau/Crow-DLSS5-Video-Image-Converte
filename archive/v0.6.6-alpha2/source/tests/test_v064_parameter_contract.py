from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
gui = (ROOT / 'src/video/VideoGuiApp.cpp').read_text(encoding='utf-8')
settings = (ROOT / 'src/video/VideoConverter.h').read_text(encoding='utf-8')
cmake = (ROOT / 'CMakeLists.txt').read_text(encoding='utf-8')
readme = (ROOT / 'README.md').read_text(encoding='utf-8')

assert 'VERSION 0.6.4' in cmake or 'VERSION 0.6.5' in cmake or 'VERSION 0.6.6' in cmake
assert ('V0.6.4 - Saved Parameters / Anti-Warp Defaults' in gui or 'V0.6.5 - External Render Data' in gui or 'V0.6.5.1 - External Data Auto Calibration' in gui or 'V0.6.5.2 - Motion Preview' in gui or 'V0.6.5.3 - Motion Preview' in gui or 'V0.6.5.3 - Adaptive Stable Motion' in gui or 'V0.6.6-alpha1 - NVOF D3D12 Foundation' in gui or 'V0.6.6-alpha2 - Native NVOF D3D12 Execute' in gui or 'Crow-DLSS5-Video-Image-Converter V0.6.6-alpha2' in gui)

# Factory anti-warp profile must be centralized and reflected in VideoSettings defaults.
for token in [
    '{IDC_FLOW_WIDTH, ParameterKind::Edit, L"flow_width", L"480"}',
    '{IDC_SCENE_CUT, ParameterKind::Edit, L"scene_cut", L"0.28"}',
    '{IDC_DENOISE_STRENGTH, ParameterKind::Edit, L"denoise_strength", L"0.72"}',
    '{IDC_DENOISE_HISTORY, ParameterKind::Edit, L"denoise_history", L"0.76"}',
    '{IDC_DENOISE_SPATIAL, ParameterKind::Edit, L"denoise_spatial", L"0.22"}',
    '{IDC_DENOISE_DETAIL, ParameterKind::Edit, L"denoise_detail_protect", L"0.86"}',
    '{IDC_DEPTH_STABILIZE, ParameterKind::Check, L"stable_depth", L"1"}',
    '{IDC_OUTPUT_STABILIZE, ParameterKind::Check, L"stable_output", L"0"}',
]:
    assert token in gui, token

for token in [
    'uint32_t flowAnalysisWidth = 480;',
    'float sceneCutThreshold = 0.28f;',
    'bool temporalDepthStabilization = true;',
    'bool temporalOutputStabilization = false;',
    'float denoiseStrength = 0.72f;',
    'float denoiseHistoryWeight = 0.76f;',
    'float denoiseSpatialStrength = 0.22f;',
    'float denoiseDetailProtection = 0.86f;',
]:
    assert token in settings, token

# Persistence must be explicit and portable next to the video runtime assets.
assert 'IDC_SAVE_PARAMETERS' in gui
assert 'L"Save Parameters"' in gui
assert 'video-parameters.ini' in gui
assert 'WritePrivateProfileStringW' in gui
assert 'GetPrivateProfileStringW' in gui
assert 'LoadSavedParameters(s)' in gui
assert 'SaveParameters(s)' in gui

# Every factory parameter is represented by one ParameterSpec and gets an individual reset button.
block = gui.split('static constexpr ParameterSpec kParameterSpecs[] = {', 1)[1].split('};', 1)[0]
ids = re.findall(r'\{(IDC_[A-Z0-9_]+),\s*ParameterKind::', block)
# Later versions may add independently persisted parameters. The original V0.6.4
# 25-control profile must remain a strict subset rather than freezing the array length.
assert len(ids) >= 25, len(ids)
assert len(set(ids)) == len(ids)
assert 'for (const auto& spec : kParameterSpecs) Make(h,L"BUTTON",L"Reset"' in gui
assert 'ResetOneParameter(s, parameterId)' in gui
assert 'Reset All' not in gui

# Reset should not overwrite persistence implicitly.
reset_body = gui.split('void ResetOneParameter', 1)[1].split('bool LoadSavedParameters', 1)[0]
assert 'WritePrivateProfileStringW' not in reset_body
assert 'Click Save Parameters to persist it' in reset_body

# V0.6.3 performance path remains inherited, not replaced by the settings UI work.
assert 'performance-last.csv' in (ROOT / 'src/video/VideoConverter.cpp').read_text(encoding='utf-8')
assert 'DLSS5_DISABLE_D3D12_BATCH' in (ROOT / 'src/DlssNrRunner.cpp').read_text(encoding='utf-8')
assert 'DLSS5_PERF_THREADS' in (ROOT / 'src/video/ParallelRows.h').read_text(encoding='utf-8')

assert 'There is deliberately no `Reset All` action.' in readme
print('PASS: V0.6.4 parameter persistence / per-control reset / anti-warp defaults contract')
