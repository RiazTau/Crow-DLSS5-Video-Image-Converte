#pragma once
#include "ImageWic.h"
#include <cstdint>
#include <vector>

namespace video {

struct MotionPreviewResult {
    Rgba8Image image;
    float robustMagnitudePixels = 0.0f; // Approximate P95 after effective XY scale.
};

// Builds an HSV-style diagnostic visualization from the final current->previous
// full-resolution motion field. Hue encodes direction and value encodes magnitude.
// The preview is downscaled for the GUI so diagnostics do not add a full-resolution
// RGBA copy on every preview tick.
MotionPreviewResult BuildMotionPreview(const std::vector<float>& motionXY,
                                       uint32_t width,
                                       uint32_t height,
                                       float scaleX = 1.0f,
                                       float scaleY = 1.0f,
                                       uint32_t maxPreviewWidth = 960);

} // namespace video
