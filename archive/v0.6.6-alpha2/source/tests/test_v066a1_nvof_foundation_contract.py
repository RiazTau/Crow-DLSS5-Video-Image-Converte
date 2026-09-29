from pathlib import Path
root=Path(__file__).resolve().parents[1]
cmake=(root/'CMakeLists.txt').read_text(encoding='utf-8-sig')
h=(root/'src/video/VideoConverter.h').read_text(encoding='utf-8-sig')
cpp=(root/'src/video/VideoConverter.cpp').read_text(encoding='utf-8-sig')
gui=(root/'src/video/VideoGuiApp.cpp').read_text(encoding='utf-8-sig')
probe=(root/'src/video/NvofRuntimeProbe.cpp').read_text(encoding='utf-8-sig')
post=(root/'src/video/NvofFlowPostprocess.cpp').read_text(encoding='utf-8-sig')
session=(root/'src/video/NvofFlowSession.cpp').read_text(encoding='utf-8-sig')
assert 'VERSION 0.6.6' in cmake
assert 'NVOF_SDK_DIR' in cmake and 'DLSS5_HAS_NVOF_SDK' in cmake
assert 'NvidiaOpticalFlow' in h
assert 'NvofFlowSession' in cpp
assert 'NVIDIA Optical Flow' in gui
assert 'nvofapi64.dll' in probe and 'NvOFAPICreateInstanceD3D12' in probe
assert 'DecodeNvofFixed11_5' in post and '1.0f / 32.0f' in post
assert 'f maps current' in post.lower()
assert 'DLSS5_NVOF_D3D12_BRIDGE_READY' in session
assert (root/'NVOF_SDK_SETUP.bat').exists()
assert (root/'scripts/setup_nvof_sdk.ps1').exists()
print('PASS: V0.6.6-alpha1 NVOF D3D12 foundation contract')
