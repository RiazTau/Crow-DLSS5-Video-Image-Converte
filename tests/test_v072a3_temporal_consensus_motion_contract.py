from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
consensus_h = (ROOT/'src/video/TemporalMotionConsensus.h').read_text(encoding='utf-8')
consensus = (ROOT/'src/video/TemporalMotionConsensus.cpp').read_text(encoding='utf-8')
flow_h = (ROOT/'src/video/TemporalFlow.h').read_text(encoding='utf-8')
converter = (ROOT/'src/video/VideoConverter.cpp').read_text(encoding='utf-8')
gui = (ROOT/'src/video/VideoGuiApp.cpp').read_text(encoding='utf-8')
cmake = (ROOT/'CMakeLists.txt').read_text(encoding='utf-8')
readme = (ROOT/'README.md').read_text(encoding='utf-8')

# New component and bounded compact history.
assert 'class TemporalMotionConsensus' in consensus_h
assert 'std::deque<HistoryGrid> _history' in consensus_h
assert '_maxHistory = mode == NvofReliabilityMode::Strong ? 3u' in consensus
assert '_step = mode == NvofReliabilityMode::Strong ? std::max(2u, grid / 2u)' in consensus

# Trajectory reprojection + temporal candidate arbitration.
assert 'traceX = px0 + curX' in consensus
assert 'traceY = py0 + curY' in consensus
assert 'traceX += vx' in consensus and 'traceY += vy' in consensus
assert 'phaseSlipLike' in consensus
assert 'historyStable' in consensus
assert ('expectedX = hx[0] + a1x' in consensus or 'predictedX = hx[0] + a1x' in consensus)  # smooth-acceleration predictor
assert 'DepthCompatible(referenceDepth, hd' in consensus
assert 'robustGlobalMotionX' in consensus

# Hybrid order: alpha2 repair -> depth-aware repair -> temporal consensus -> FG.
assert 'RefineReliableMotionWithDepth(fgFlow' in converter
assert 'temporalConsensus->Stabilize(fgFlow, depthPtr, temporalReset)' in converter
assert 'fgMotionPtr = &fgFlow.motionXY' in converter
assert ('External EXR motion is renderer guidance and is never rewritten here.' in converter or 'External EXR motion remains renderer guidance and is not rewritten.' in converter)

# Scene cut reset and diagnostics.
assert 'if (reset || current.sceneCut)' in consensus
for token in ['temporalConsensusCorrectedFraction', 'temporalConsensusMeanResidual', 'temporalConsensusHistory']:
    assert token in flow_h
for token in ['temporal_consensus_fraction', 'temporal_consensus_residual', 'temporal_consensus_history']:
    assert token in converter

# UI reuses the existing single control instead of adding a parameter wall.
assert ('Auto - Reliable + 3F Consensus' in gui or 'Auto - Dual Path / FG 3F' in gui or 'Auto - Spatial + NR Safe / FG 3F' in gui or 'Auto - Visibility + Spatial / NR Safe / FG 3F' in gui or 'Auto - Predictive Multi-Scale / NR Safe / FG 3F' in gui)
assert ('Strong - Up to 5F Consensus' in gui or 'Strong - Dual Path / FG 5F' in gui or 'Strong - Spatial + NR Safe / FG 5F' in gui or 'Strong - Visibility + Spatial / NR Safe / FG 5F' in gui or 'Strong - Predictive 3-Scale / NR Safe / FG 5F' in gui)
assert 'fg_motion_stabilization' in gui
assert ('Crow - DLSS Rendering Tool V0.7.2-alpha3' in gui or 'Crow - DLSS Rendering Tool V0.7.2-alpha4' in gui or 'Crow - DLSS Rendering Tool V0.7.2-alpha5' in gui or 'Crow - DLSS Rendering Tool V0.7.2-alpha6' in gui or 'Crow - DLSS Rendering Tool V0.7.3-alpha1' in gui)

# Build and unit-test contract.
assert 'src/video/TemporalMotionConsensus.cpp' in cmake
assert 'test-temporal-motion-consensus' in cmake
assert 'Temporal Consensus Motion' in readme
print('PASS: V0.7.2-alpha3 Temporal Consensus Motion contract')
