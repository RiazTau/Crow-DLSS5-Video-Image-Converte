#pragma once

#include "ImageWic.h"
#include "TemporalFlow.h"
#include "NvofConfig.h"
#include <cstdint>
#include <vector>

namespace video {

// NVOFA optical-flow output stores each vector component as a signed fixed-point (S10.5)
// value. The raw 16-bit integer is therefore divided by 32 to obtain pixel displacement.
float DecodeNvofFixed11_5(int16_t value) noexcept;

struct NvofPackedVector {
    int16_t x = 0;
    int16_t y = 0;
};

struct NvofPostprocessInput {
    uint32_t sourceWidth = 0;
    uint32_t sourceHeight = 0;
    uint32_t gridWidth = 0;
    uint32_t gridHeight = 0;

    // IMPORTANT: configure the hardware call as input=current and reference=previous.
    // NVIDIA calls that the forward direction; for this application it is already the
    // required internal current -> previous motion convention.
    const std::vector<NvofPackedVector>* forward = nullptr;

    // Optional NVOFA backward result: previous -> current. Used for FB consistency.
    const std::vector<NvofPackedVector>* backward = nullptr;

    // Optional 8-bit NVOFA cost. Higher cost means less reliable motion.
    const std::vector<uint8_t>* forwardCost = nullptr;
    const std::vector<uint8_t>* backwardCost = nullptr;

    // Optional source frames for a photometric reprojection confidence term.
    const Rgba8Image* previousFrame = nullptr;
    const Rgba8Image* currentFrame = nullptr;

    float sceneCutScore = 0.0f;
    bool sceneCut = false;
    uint32_t flowGridSize = 4;
    NvofReliabilityMode reliabilityMode = NvofReliabilityMode::Off;
};

// Converts the hardware grid to full-resolution current->previous pixels and constructs
// confidence plus alpha6 soft visibility / occlusion / disocclusion / uncertainty fields
// from NVOFA cost, bidirectional consistency, landing topology and photometric residual.
// Repair remains bounded so confident vectors are preserved unless independent evidence disagrees.
TemporalFlowResult PostprocessNvofFlow(const NvofPostprocessInput& input);

// Builds the conservative NR branch from the same NVOF reliability/visibility analysis used by FG.
// It starts from raw NVOF, accepts only bounded correction toward the reliable candidate,
// and rejects temporal history explicitly when visibility/disocclusion/uncertainty says the
// previous frame has no safe source correspondence.
TemporalFlowResult BuildNrSafeReliableMotion(const TemporalFlowResult& flow,
                                             const std::vector<float>* depth,
                                             uint32_t width,
                                             uint32_t height,
                                             NvofReliabilityMode mode,
                                             uint32_t neighborhoodStep);

// Alpha6 post-spatial active-reconstruction pass for FG. It always re-evaluates motion
// after multi-scale uncertainty is available; optional depth adds a boundary gate that
// prevents repaired vectors from crossing strong foreground/background discontinuities.
void RefineReliableMotionWithDepth(TemporalFlowResult& flow,
                                   const std::vector<float>* depth,
                                   uint32_t width,
                                   uint32_t height,
                                   NvofReliabilityMode mode,
                                   uint32_t neighborhoodStep);

} // namespace video
