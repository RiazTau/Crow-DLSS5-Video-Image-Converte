from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spatial_h = (ROOT/'src/video/SpatialMotionConsensus.h').read_text(encoding='utf-8')
spatial = (ROOT/'src/video/SpatialMotionConsensus.cpp').read_text(encoding='utf-8')
post = (ROOT/'src/video/NvofFlowPostprocess.cpp').read_text(encoding='utf-8')
consensus = (ROOT/'src/video/TemporalMotionConsensus.cpp').read_text(encoding='utf-8')
converter = (ROOT/'src/video/VideoConverter.cpp').read_text(encoding='utf-8')
gui = (ROOT/'src/video/VideoGuiApp.cpp').read_text(encoding='utf-8')
readme = (ROOT/'README.md').read_text(encoding='utf-8')
cmake = (ROOT/'CMakeLists.txt').read_text(encoding='utf-8')
cn_build = (ROOT/'scripts/build_cn.ps1').read_text(encoding='utf-8-sig')

# Anti-aliased multi-scale motion pyramid.
assert 'DownsampleQuarterForMotion' in spatial_h and 'DownsampleQuarterForMotion' in spatial
assert 'static constexpr int k[5] = {1, 4, 6, 4, 1}' in spatial
assert 'float priorStrength' in spatial_h and 'priorStrength=std::clamp' in spatial
assert 'quarterNvofFlow' in converter
assert 'settings.nvof.reliability == NvofReliabilityMode::Strong' in converter
assert 'DownsampleQuarterForMotion(input)' in converter
assert 'settings.nvof.reliability, 0.62f' in converter
assert '1.0f-(1.0f-previousFraction)*(1.0f-thisFraction)' in spatial

# Active motion reconstruction: dominant local mode + affine camera fallback.
assert 'RobustAffineGlobalMotion' in post
assert 'struct AffineMotionModel' in post
assert 'Solve3x3' in post
assert 'robustWeight' in post
assert 'weighted vector medoid' in post
assert 'localCoherence' in post
assert 'effectiveTrust' in post and 'independentlyProtected' in post
assert 'RepairRejectedMotion(candidate, depth' in post
assert 'if (!depth || depth->size()' not in post
assert 'globalModel.Evaluate' in post
assert 'localModeRepairedFraction' in post and 'affineFallbackFraction' in post
assert 'local_mode_repair_fraction' in converter and 'affine_fallback_fraction' in converter

# Predictive temporal state rather than trailing median only.
assert 'historyReliability' in consensus
assert 'predictedX = hx[0] + a1x' in consensus
assert 'accelAgreement' in consensus
assert 'uncertaintyScale' in consensus
assert 'curUncertainty > 0.45f' in consensus
assert '(1.0f - 0.42f * historyUncertainty)' in consensus

# Compact UI and retained build fixes.
assert 'Auto - Predictive Multi-Scale / NR Safe / FG 3F' in gui
assert 'Strong - Predictive 3-Scale / NR Safe / FG 5F' in gui
assert ('Crow - DLSS Rendering Tool V0.7.2-alpha6' in gui or 'Crow - DLSS Rendering Tool V0.7.3-alpha1' in gui)
assert 'Predictive Multi-Scale Motion Reconstruction' in readme
assert 'set(_NVAPI_HEADER "${NVAPI_SDK_DIR}/nvapi.h")' in cmake
assert "$ErrorActionPreference = 'Continue'" in cn_build
assert '$code = $LASTEXITCODE' in cn_build

print('PASS: V0.7.2-alpha6 Predictive Multi-Scale Motion Reconstruction contract')
