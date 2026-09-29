from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
flow_h = (ROOT/'src/video/TemporalFlow.h').read_text(encoding='utf-8')
post_h = (ROOT/'src/video/NvofFlowPostprocess.h').read_text(encoding='utf-8')
post = (ROOT/'src/video/NvofFlowPostprocess.cpp').read_text(encoding='utf-8')
converter = (ROOT/'src/video/VideoConverter.cpp').read_text(encoding='utf-8')
gui = (ROOT/'src/video/VideoGuiApp.cpp').read_text(encoding='utf-8')
spatial_h = (ROOT/'src/video/SpatialMotionConsensus.h').read_text(encoding='utf-8')
spatial = (ROOT/'src/video/SpatialMotionConsensus.cpp').read_text(encoding='utf-8')
cmake = (ROOT/'CMakeLists.txt').read_text(encoding='utf-8')
readme = (ROOT/'README.md').read_text(encoding='utf-8')

# Spatial scheme 1: half-resolution NVOF provides an independent low-frequency vote.
assert 'DownsampleHalfForMotion' in spatial_h and 'FuseCoarseNvofMotion' in spatial_h
assert 'coarseNvofFlow' in converter and 'DownsampleHalfForMotion(input)' in converter
assert 'FuseCoarseNvofMotion(flow, coarseFlow' in converter
assert 'spatialConsensusCorrectedFraction' in flow_h
assert 'spatial_consensus_fraction' in converter and 'spatial_consensus_residual' in converter
assert 'src/video/SpatialMotionConsensus.cpp' in cmake
assert 'test-spatial-motion-consensus' in cmake

# Shared analysis keeps raw NVOF plus corrected candidate.
assert 'std::vector<float> rawMotionXY' in flow_h
assert 'result.rawMotionXY = result.motionXY' in post
assert 'BuildNrSafeReliableMotion' in post_h and 'BuildNrSafeReliableMotion' in post

# NR-safe path is explicitly conservative and history-aware.
for token in ['protectConfidence', 'hardReject', 'maxBlend', 'nrSafeCorrectedFraction', 'nrHistoryRejectedFraction']:
    assert token in post
assert 'out.confidence[i] < 0.08f' in post
assert 'Reprojection outside the image is never valid history.' in post

# Main pipeline uses distinct NR and FG fields.
assert 'TemporalFlowResult nrPreFlow = flow' in converter
assert 'TemporalFlowResult nrFlow = nrPreFlow' in converter
assert 'TemporalFlowResult fgFlow = flow' in converter
assert 'BuildNrSafeReliableMotion(flow, nullptr' in converter
assert 'BuildNrSafeReliableMotion(flow, depthPtr' in converter
assert 'RefineReliableMotionWithDepth(fgFlow' in converter
assert 'temporalConsensus->Stabilize(fgFlow, depthPtr, temporalReset)' in converter
assert 'runner->ProcessTemporal(nrInput, depthPtr, nrMotionPtr' in converter
assert '? nrFlow : flow' in converter
assert 'fgMotionPtr ? *fgMotionPtr : flow.motionXY' in converter

# Diagnostics make NR-vs-FG behavior observable.
assert 'nr_safe_corrected_fraction' in converter
assert 'nr_history_rejected_fraction' in converter

# One compact user control now applies to NR and FG; NR-only must not force it off.
assert 'NR / FG Motion Stabilization' in gui
assert ('Auto - Spatial + NR Safe / FG 3F' in gui or 'Auto - Visibility + Spatial / NR Safe / FG 3F' in gui or 'Auto - Predictive Multi-Scale / NR Safe / FG 3F' in gui)
assert ('Strong - Spatial + NR Safe / FG 5F' in gui or 'Strong - Visibility + Spatial / NR Safe / FG 5F' in gui or 'Strong - Predictive 3-Scale / NR Safe / FG 5F' in gui)
assert 'nvof && (nrEnabled || fgEnabled)' in gui
assert 'if (!v.enableDlssNr && !v.enableFrameGeneration2X) v.nvof.reliability' in gui
assert '(v.enableDlssNr || v.enableFrameGeneration2X) && v.temporalMode == video::TemporalMode::NvidiaOpticalFlow' in gui

assert ('Crow - DLSS Rendering Tool V0.7.2-alpha4' in gui or 'Crow - DLSS Rendering Tool V0.7.2-alpha5' in gui or 'Crow - DLSS Rendering Tool V0.7.2-alpha6' in gui or 'Crow - DLSS Rendering Tool V0.7.3-alpha1' in gui)
assert 'Spatial-Temporal Dual-Path Motion' in readme
print('PASS: V0.7.2-alpha4 Dual-Path Reliable Motion contract')
