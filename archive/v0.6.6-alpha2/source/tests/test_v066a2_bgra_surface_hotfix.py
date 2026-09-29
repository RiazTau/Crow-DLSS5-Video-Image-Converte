from pathlib import Path
root = Path(__file__).resolve().parents[1]
bridge = (root / 'src/video/NvofD3D12Bridge.cpp').read_text(encoding='utf-8')
assert 'DXGI_FORMAT_B8G8R8A8_UNORM' in bridge
assert 'inputNeedsRgbaToBgraSwizzle = true' in bridge
assert 'SurfaceFormats(NV_OF_BUFFER_USAGE_INPUT)' in bridge
assert 'currentUploadScratch' in bridge and 'previousUploadScratch' in bridge
assert 'bgra[i + 0] = rgba[i + 2]' in bridge
assert 'bgra[i + 1] = rgba[i + 1]' in bridge
assert 'bgra[i + 2] = rgba[i + 0]' in bridge
assert 'bgra[i + 3] = rgba[i + 3]' in bridge
assert 'CreateTexture2D(inputTextureFormat' in bridge
print('PASS: V0.6.6-alpha2 NVOF D3D12 ABGR/BGRA surface hotfix contract')
