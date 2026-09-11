#pragma once
#include "ImageWic.h"
#include "TemporalFlow.h"
#include <cstdint>
#include <vector>

namespace video {

enum class DenoiseMode {
    Off = 0,
    TemporalHq = 1,
    FullHq = 2,
};

struct TemporalDenoiseSettings {
    DenoiseMode mode = DenoiseMode::FullHq;
    // Master mix. 0 keeps the source untouched; 1 uses the configured HQ filter fully.
    float strength = 0.85f;
    // Maximum history contribution in stable, high-confidence areas.
    float historyWeight = 0.90f;
    // Edge-aware 3x3 fallback used for new/disoccluded/low-confidence pixels.
    float spatialStrength = 0.32f;
    // 0 = maximum smoothing, 1 = aggressively preserve local contrast/edges.
    float detailProtection = 0.72f;
    // Temporal history ramp. Long stable runs become progressively cleaner.
    uint32_t maxHistory = 24;
    // Reject history when motion compensation disagrees with the current frame.
    float lumaReject = 0.14f;
    float chromaReject = 0.18f;
    // Variance/AABB expansion for noisy material so noise is not mistaken for a disocclusion.
    float clampExpansion = 0.025f;
};

struct TemporalDenoiseStats {
    float averageHistoryWeight = 0.0f;
    float averageSpatialWeight = 0.0f;
    float rejectedFraction = 0.0f;
    float averageHistoryAge = 0.0f;
};

// V0.6 causal, motion-compensated denoiser. It deliberately uses only current and
// previous history so the FFmpeg stream remains one-pass and memory bounded.
class TemporalDenoiser {
public:
    explicit TemporalDenoiser(TemporalDenoiseSettings settings = {});

    void Reset();

    Rgba8Image Process(const Rgba8Image& current,
                       const TemporalFlowResult* flow,
                       bool resetHistory,
                       TemporalDenoiseStats* stats = nullptr);

private:
    TemporalDenoiseSettings _settings;
    Rgba8Image _history;
    std::vector<uint8_t> _historyAge;
};

} // namespace video
