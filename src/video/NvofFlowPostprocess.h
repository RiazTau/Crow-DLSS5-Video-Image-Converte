#pragma once

#include "ImageWic.h"
#include "TemporalFlow.h"
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

    // Optional source frames for a photometric reprojection confidence term.
    const Rgba8Image* previousFrame = nullptr;
    const Rgba8Image* currentFrame = nullptr;

    float sceneCutScore = 0.0f;
    bool sceneCut = false;
};

// Converts the hardware grid to full-resolution current->previous pixels and constructs
// a confidence field from NVOFA cost + forward/backward consistency + photometric residual.
// The confidence is diagnostic/temporal guidance only; DLSSNR still receives motionXY.
TemporalFlowResult PostprocessNvofFlow(const NvofPostprocessInput& input);

} // namespace video
