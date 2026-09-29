#pragma once

#include "NvofConfig.h"
#include "TemporalFlow.h"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace video {

// V0.7.2-alpha6: visibility-gated trailing multi-frame motion consensus for FG guidance.
//
// The alpha2 pass decides whether the *current* NVOF vector is trustworthy using
// hardware cost, forward/backward consistency, photometric error and optional depth.
// This class adds a different signal: trajectory consistency over time. It follows
// current->previous motion through a compact history lattice and looks for a sudden
// one-frame phase slip relative to the same reprojected surface. Alpha5 additionally
// refuses to traverse low-visibility / high-uncertainty history, preventing stale motion
// from being applied to newly revealed surfaces.
//
// Auto uses a short trailing history and is deliberately conservative. Strong keeps
// a longer history (up to a five-source-frame trajectory) and is more willing to
// override a locally plausible but temporally impossible periodic-texture vector.
class TemporalMotionConsensus {
public:
    TemporalMotionConsensus(uint32_t width,
                            uint32_t height,
                            NvofReliabilityMode mode,
                            uint32_t nvofGridSize);

    void Reset();

    // Returns a copy of current with temporal-consensus corrections applied.  The depth
    // pointer is optional; when present, history is only followed through compatible
    // depth layers.  reset/scene-cut clears history and returns current unchanged.
    TemporalFlowResult Stabilize(const TemporalFlowResult& current,
                                 const std::vector<float>* depth,
                                 bool reset);

private:
    struct HistoryGrid {
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t step = 1;
        std::vector<float> motionXY;
        std::vector<float> confidence;
        std::vector<float> visibility;
        std::vector<float> uncertainty;
        std::vector<float> depth;
    };

    uint32_t _width = 0;
    uint32_t _height = 0;
    uint32_t _step = 2;
    NvofReliabilityMode _mode = NvofReliabilityMode::Off;
    size_t _maxHistory = 0;
    std::deque<HistoryGrid> _history; // newest first

    HistoryGrid MakeHistoryGrid(const TemporalFlowResult& flow,
                                const std::vector<float>* depth) const;
    void PushHistory(HistoryGrid grid);
};

} // namespace video
