from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
flow_h = (ROOT/'src/video/TemporalFlow.h').read_text(encoding='utf-8')
post = (ROOT/'src/video/NvofFlowPostprocess.cpp').read_text(encoding='utf-8')
spatial = (ROOT/'src/video/SpatialMotionConsensus.cpp').read_text(encoding='utf-8')
consensus_h = (ROOT/'src/video/TemporalMotionConsensus.h').read_text(encoding='utf-8')
consensus = (ROOT/'src/video/TemporalMotionConsensus.cpp').read_text(encoding='utf-8')
converter = (ROOT/'src/video/VideoConverter.cpp').read_text(encoding='utf-8')
gui = (ROOT/'src/video/VideoGuiApp.cpp').read_text(encoding='utf-8')
cmake = (ROOT/'CMakeLists.txt').read_text(encoding='utf-8')
cn_auto = (ROOT/'scripts/auto_build_cn.ps1').read_text(encoding='utf-8-sig')
cn_build = (ROOT/'scripts/build_cn.ps1').read_text(encoding='utf-8-sig')
readme = (ROOT/'README.md').read_text(encoding='utf-8')

# New continuous motion-state fields.
for token in ['historyVisibility', 'occlusionProbability', 'disocclusionProbability', 'motionUncertainty']:
    assert token in flow_h
for token in ['meanHistoryVisibility', 'disoccludedFraction', 'occlusionAmbiguousFraction', 'highUncertaintyFraction']:
    assert token in flow_h

# Visibility topology comes from directional landing coverage, not a binary confidence alias.
assert 'BuildLandingCoverage' in post
assert 'forwardLanding' in post and 'backwardLanding' in post
assert 'reverseCoverage' in post and 'forwardDensity' in post
assert 'fbVisibility' in post
assert 'historyVisibility[i] = historyVisibility' in post
assert 'disocclusionProbability[i] = disocclusion' in post
assert 'occlusionProbability[i] = occlusionAmbiguity' in post
assert 'motionUncertainty[i]' in post

# NR explicitly rejects unsafe history while keeping vector arbitration separate.
assert 'historyConfidence = conf * visibility' in post
assert 'disocclusion >= 0.80f' in post
assert 'historyConfidence = 0.0f' in post
assert 'targetDisocclusion < 0.45f' in post
assert 'minNeighborVisibility' in post and 'maxNeighborUncertainty' in post

# Spatial and temporal stages consume uncertainty/visibility rather than silently discarding it.
assert 'coarseHasUncertainty' in spatial
assert 'out.motionUncertainty[i]' in spatial
assert 'std::vector<float> visibility' in consensus_h
assert 'std::vector<float> uncertainty' in consensus_h
assert 'curDisocclusion > 0.72f' in consensus
assert 'visibility < cfg.minimumVisibility' in consensus
assert 'uncertainty > cfg.maximumUncertainty' in consensus

# Diagnostics and compact user control.
for token in ['mean_history_visibility', 'disoccluded_fraction', 'occlusion_ambiguous_fraction', 'high_uncertainty_fraction']:
    assert token in converter
assert ('Auto - Visibility + Spatial / NR Safe / FG 3F' in gui or 'Auto - Predictive Multi-Scale / NR Safe / FG 3F' in gui)
assert ('Strong - Visibility + Spatial / NR Safe / FG 5F' in gui or 'Strong - Predictive 3-Scale / NR Safe / FG 5F' in gui)
assert ('Crow - DLSS Rendering Tool V0.7.2-alpha5' in gui or 'Crow - DLSS Rendering Tool V0.7.2-alpha6' in gui or 'Crow - DLSS Rendering Tool V0.7.3-alpha1' in gui)

# Build hotfixes discovered during alpha5 work are retained.
assert 'set(_NVAPI_HEADER "${NVAPI_SDK_DIR}/nvapi.h")' in cmake
assert 'if(EXISTS "${_NVAPI_HEADER}" AND EXISTS "${_NVAPI_SETTINGS_HEADER}" AND EXISTS "${_NVAPI_LIBRARY}")' in cmake
assert 'find_path(NVAPI_INCLUDE_DIR nvapi.h' not in cmake
assert "$ErrorActionPreference = 'Continue'" in cn_build
assert '$code = $LASTEXITCODE' in cn_build
assert 'scripts\\build_cn.ps1' not in cn_auto or 'build_cn.ps1' in cn_auto

assert 'Visibility-Aware Uncertainty Motion' in readme
print('PASS: V0.7.2-alpha5 Visibility-Aware Uncertainty Motion contract')
