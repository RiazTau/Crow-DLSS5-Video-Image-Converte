from pathlib import Path
root = Path(__file__).resolve().parents[1]
cmake = (root/'CMakeLists.txt').read_text(encoding='utf-8')
h = (root/'src/video/VideoConverter.h').read_text(encoding='utf-8')
cpp = (root/'src/video/VideoConverter.cpp').read_text(encoding='utf-8')
ext_h = (root/'src/video/ExternalRenderData.h').read_text(encoding='utf-8')
ext_cpp = (root/'src/video/ExternalRenderData.cpp').read_text(encoding='utf-8')
seq = (root/'src/video/ExternalSequence.cpp').read_text(encoding='utf-8')
gui = (root/'src/video/VideoGuiApp.cpp').read_text(encoding='utf-8')
dlg = (root/'src/video/ExternalRenderDataDialog.cpp').read_text(encoding='utf-8')
img_h = (root/'src/ImageImport.h').read_text(encoding='utf-8')

assert 'VERSION 0.6.5' in cmake or 'VERSION 0.6.6' in cmake
for src in ['src/video/ExternalSequence.cpp','src/video/ExternalRenderData.cpp','src/video/ExternalRenderDataDialog.cpp']:
    assert src in cmake
assert 'ExternalExr = 2' in h
assert 'ExternalExr = 3' in h
assert 'ExternalRenderDataSettings externalData' in h
assert 'LoadExrFloatChannel' in img_h
assert 'ParseExrSequencePattern' in seq
assert 'filename must end in a frame number' in seq
assert 'ExternalDepthMapping::FixedRange' in ext_cpp
assert 'farValue <= _settings.depth.nearValue' in ext_cpp
assert 'External motion X' in ext_cpp and 'External motion Y' in ext_cpp
assert 'current -> previous' in ext_h
assert 'exp(-residual * 5.0f)' in ext_cpp
assert 'externalData->Validate()' in cpp
assert 'externalData->LoadMotion' in cpp
assert 'externalData->LoadDepth' in cpp
assert 'External EXR Sequence' in gui
assert 'External EXR Motion - CG Ground Truth' in gui
assert 'External Render Data...' in gui
assert 'video-parameters.ini' in gui
assert 'ExternalRenderData' in gui
assert 'Sequence paths apply to this session only' in gui
assert 'Quick Auto' in dlg and 'Auto Calibrate' in dlg
assert 'Fixed Near/Far' in dlg and 'Raw 0..1' in dlg
assert 'Flip X' in dlg and 'Flip Y' in dlg
assert 'IDC_D_CHANNEL_RESET' in dlg and 'IDC_M_X_RESET' in dlg and 'IDC_M_Y_RESET' in dlg
print('PASS: V0.6.5 External Render Data contract')
