from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
cmake = (ROOT / 'CMakeLists.txt').read_text(encoding='utf-8')
helper = (ROOT / 'auto_depth' / 'dis_flow_video.py').read_text(encoding='utf-8')
readme = (ROOT / 'README.md').read_text(encoding='utf-8')

assert 'VERSION 0.6.5.3' in cmake or 'VERSION 0.6.6' in cmake
assert 'make_dis(False)' in helper
assert 'make_dis(True)' in helper
assert 'needs_spatial_rescue' in helper
assert 'candidate_score' in helper
assert 'is_near_duplicate' in helper
assert 'zero_motion_confidence' in helper
assert 'condition_isolated_outliers' in helper
assert 'confidence_from_flow' in helper
assert 'zero-near-duplicate' in helper
assert 'Adaptive Stable Motion' in readme
assert 'NVIDIA Optical Flow' in (ROOT / 'docs' / 'RESEARCH_OPTICAL_FLOW_BACKENDS_2026-09-04.md').read_text(encoding='utf-8')
print('PASS: V0.6.5.3 adaptive stable motion contract')
