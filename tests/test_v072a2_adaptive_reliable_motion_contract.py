from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
bridge = (ROOT/'src/video/NvofD3D12Bridge.cpp').read_text(encoding='utf-8')
bridge_h = (ROOT/'src/video/NvofD3D12Bridge.h').read_text(encoding='utf-8')
post = (ROOT/'src/video/NvofFlowPostprocess.cpp').read_text(encoding='utf-8')
post_h = (ROOT/'src/video/NvofFlowPostprocess.h').read_text(encoding='utf-8')
session = (ROOT/'src/video/NvofFlowSession.cpp').read_text(encoding='utf-8')
config = (ROOT/'src/video/NvofConfig.h').read_text(encoding='utf-8')
converter = (ROOT/'src/video/VideoConverter.cpp').read_text(encoding='utf-8')
gui = (ROOT/'src/video/VideoGuiApp.cpp').read_text(encoding='utf-8')
cmake = (ROOT/'CMakeLists.txt').read_text(encoding='utf-8')
readme = (ROOT/'README.md').read_text(encoding='utf-8')

assert 'NV_OF_PRED_DIRECTION_BOTH' in bridge and 'NV_OF_PRED_DIRECTION_FORWARD' in bridge
assert 'out.bwdOutputBuffer = reliableMotionEnabled ? backwardFlowHandle : nullptr' in bridge
assert 'out.bwdOutputCostBuffer = (reliableMotionEnabled && outputCostEnabled) ? backwardCostHandle : nullptr' in bridge
assert 'backwardFlowTexture' in bridge and 'backwardCostTexture' in bridge
assert 'std::vector<NvofPackedVector> backward' in bridge_h
assert 'std::vector<uint8_t> backwardCost' in bridge_h
assert 'pp.backward = native.backward.empty() ? nullptr : &native.backward' in session
assert 'pp.backwardCost = native.backwardCost.empty() ? nullptr : &native.backwardCost' in session
assert 'NvofReliabilityMode' in config
assert 'RepairRejectedMotion' in post
assert ('RobustGlobalMotion' in post or 'RobustAffineGlobalMotion' in post)
assert 'RefineReliableMotionWithDepth' in post and 'DepthCompatible' in post
assert 'costPair' in post and 'confFb' in post
assert 'RefineReliableMotionWithDepth(fgFlow' in converter
assert 'fgMotionPtr ? *fgMotionPtr : flow.motionXY' in converter
assert 'repaired_fraction' in converter and 'global_mv_x' in converter
assert ('FG Motion Stabilization' in gui or 'NR / FG Motion Stabilization' in gui)
assert ('Auto - Reliable Motion' in gui or 'Auto - Reliable + 3F Consensus' in gui or 'Auto - Dual Path / FG 3F' in gui or 'Auto - Spatial + NR Safe / FG 3F' in gui or 'Auto - Visibility + Spatial / NR Safe / FG 3F' in gui or 'Auto - Predictive Multi-Scale / NR Safe / FG 3F' in gui)
assert ('Strong - Periodic Texture' in gui or 'Strong - Up to 5F Consensus' in gui or 'Strong - Dual Path / FG 5F' in gui or 'Strong - Spatial + NR Safe / FG 5F' in gui or 'Strong - Visibility + Spatial / NR Safe / FG 5F' in gui or 'Strong - Predictive 3-Scale / NR Safe / FG 5F' in gui)
assert 'fg_motion_stabilization' in gui
assert 'NV_OF_PRED_DIRECTION_BOTH' in cmake
assert ('V0.7.2-alpha2' in gui or 'V0.7.2-alpha3' in gui or 'V0.7.2-alpha4' in gui or 'V0.7.2-alpha5' in gui or 'V0.7.2-alpha6' in gui or 'V0.7.3-alpha1' in gui)
assert 'Adaptive Reliable Motion' in readme
print('PASS: V0.7.2-alpha2 Adaptive Reliable Motion contract')
