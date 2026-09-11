from pathlib import Path
root=Path(__file__).resolve().parents[1]
cmake=(root/'CMakeLists.txt').read_text(encoding='utf-8-sig')
bridge=(root/'src/video/NvofD3D12Bridge.cpp').read_text(encoding='utf-8-sig')
session=(root/'src/video/NvofFlowSession.cpp').read_text(encoding='utf-8-sig')
selftest=(root/'src/tools/NvofExecuteSelfTest.cpp').read_text(encoding='utf-8-sig')
post=(root/'src/video/NvofFlowPostprocess.cpp').read_text(encoding='utf-8-sig')
assert 'DLSS5_NVOF_D3D12_ABI_OK' in cmake and 'check_cxx_source_compiles' in cmake
assert 'nvOFExecuteD3D12' in bridge and 'nvOFRegisterResourceD3D12' in bridge
assert 'NV_OF_FENCE_POINT' in bridge and 'numFencePoints = 1' in bridge
assert 'in.inputFrame = currentHandle' in bridge and 'in.referenceFrame = previousHandle' in bridge
assert 'NV_OF_OUTPUT_VECTOR_GRID_SIZE_4' in bridge and 'NV_OF_OUTPUT_VECTOR_GRID_SIZE_2' in bridge and 'NV_OF_OUTPUT_VECTOR_GRID_SIZE_1' in bridge
assert 'NV_OF_BUFFER_FORMAT_ABGR8' in bridge and 'enableOutputCost = outputCostEnabled ? NV_OF_TRUE : NV_OF_FALSE' in bridge
assert 'DXGI_FORMAT_B8G8R8A8_UNORM' in bridge and 'inputNeedsRgbaToBgraSwizzle' in bridge
assert 'SurfaceFormats(NV_OF_BUFFER_USAGE_INPUT)' in bridge
assert 'DXGI_FORMAT_R16G16_SINT' in bridge
assert 'DXGI_FORMAT_R8_UINT' in bridge
assert 'ExecuteCurrentToPrevious' in session
assert 'PostprocessNvofFlow' in session
assert '1.0f / 32.0f' in post
assert 'Expected flow' in selftest and 'current->previous' in selftest
assert (root/'NVOF_EXECUTE_SELFTEST.bat').exists()
print('PASS: V0.6.6-alpha2 native NVOF D3D12 execute contract')
