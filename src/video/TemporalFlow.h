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
    std::vector<float> motionXY;
    // Per-pixel confidence in [0,1]. Used by the stabilizers, not passed to NGX.
    std::vector<float> confidence;
    float sceneCutScore = 0.0f;
    bool sceneCut = false;
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
