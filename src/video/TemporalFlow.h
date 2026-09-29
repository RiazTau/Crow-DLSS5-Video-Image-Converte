#pragma once
#include "ImageWic.h"
#include <cstdint>
#include <vector>

namespace video {

struct TemporalFlowSettings {
    uint32_t analysisWidth = 320;
    uint32_t blockSize = 8;
    uint32_t searchRadius = 6;
    float sceneCutThreshold = 0.32f;
    float depthHistoryWeight = 0.20f;
    float depthRejectThreshold = 0.08f;
    float outputHistoryWeight = 0.14f;
    float outputRejectThreshold = 0.12f;
};

struct TemporalFlowResult {
    // Current -> previous motion in full-resolution pixel units, XY interleaved.
    // motionXY is the active/corrected field for the current consumer.
    std::vector<float> motionXY;
    // V0.7.2-alpha6 keeps the original NVOF field so NR and FG can derive different
    // correction policies from the same reliability analysis. Non-NVOF backends may leave
    // this empty, in which case motionXY is treated as the raw field.
    std::vector<float> rawMotionXY;
    // Per-pixel confidence in [0,1]. Used by the stabilizers, not passed to NGX.
    std::vector<float> confidence;
    // V0.7.2-alpha6 visibility-aware motion state. These fields stay continuous rather
    // than becoming binary masks so downstream NR/FG policies can choose their own risk
    // tolerance. historyVisibility answers whether current->previous reprojection has a
    // plausible visible source; occlusion/disocclusion describe correspondence topology;
    // motionUncertainty aggregates evidence disagreement without overwriting confidence.
    std::vector<float> historyVisibility;
    std::vector<float> occlusionProbability;
    std::vector<float> disocclusionProbability;
    std::vector<float> motionUncertainty;
    float sceneCutScore = 0.0f;
    bool sceneCut = false;
    // Diagnostic values populated by Adaptive Reliable Motion.
    float repairedFraction = 0.0f;
    // Alpha6 reconstruction diagnostics split the repaired fraction by candidate source.
    float localModeRepairedFraction = 0.0f;
    float affineFallbackFraction = 0.0f;
    float robustGlobalMotionX = 0.0f;
    float robustGlobalMotionY = 0.0f;
    // V0.7.2-alpha3 trailing multi-frame motion-consensus diagnostics.
    float temporalConsensusCorrectedFraction = 0.0f;
    float temporalConsensusMeanResidual = 0.0f;
    uint32_t temporalConsensusHistory = 0;
    // V0.7.2-alpha6 NR-safe branch diagnostics. NR correction is deliberately more
    // conservative than FG correction; very uncertain history is rejected rather than
    // being fully replaced by global motion.
    float nrSafeCorrectedFraction = 0.0f;
    float nrHistoryRejectedFraction = 0.0f;
    // V0.7.2-alpha6 half-resolution spatial-prior diagnostics.
    float spatialConsensusCorrectedFraction = 0.0f;
    float spatialConsensusMeanResidual = 0.0f;
    // V0.7.2-alpha6 soft-visibility diagnostics.
    float meanHistoryVisibility = 1.0f;
    float disoccludedFraction = 0.0f;
    float occlusionAmbiguousFraction = 0.0f;
    float highUncertaintyFraction = 0.0f;
};

class TemporalFlowEstimator {
public:
    explicit TemporalFlowEstimator(TemporalFlowSettings settings = {});

    TemporalFlowResult Estimate(const Rgba8Image& previous, const Rgba8Image& current) const;

    std::vector<float> StabilizeDepth(const std::vector<float>& currentDepth,
                                      const std::vector<float>& previousStableDepth,
                                      uint32_t width,
                                      uint32_t height,
                                      const TemporalFlowResult& flow) const;

    Rgba8Image StabilizeOutput(const Rgba8Image& currentOutput,
                               const Rgba8Image& previousStableOutput,
                               const TemporalFlowResult& flow) const;

private:
    TemporalFlowSettings _settings;
};

} // namespace video
