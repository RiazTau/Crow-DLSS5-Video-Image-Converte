#pragma once
#include "ExternalRenderData.h"
#include "ImageWic.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace video {

struct ExternalDepthCalibrationResult {
    bool success = false;
    std::string channel;
    ExternalDepthMapping mapping = ExternalDepthMapping::FixedRange;
    float nearValue = 0.1f;
    float farValue = 100.0f;
    bool depthInverted = false;
    float confidence = 0.0f;
    size_t sampledFrames = 0;
    std::string summary;
};

struct MotionCalibrationFramePair {
    // Current video frame number, one-based. `previous` is frame N-1 and
    // `current` is frame N. Images may be downscaled for calibration.
    uint64_t currentOneBasedFrame = 0;
    Rgba8Image previous;
    Rgba8Image current;
};

struct ExternalMotionCalibrationResult {
    bool success = false;
    std::string xChannel;
    std::string yChannel;
    float importScaleX = 1.0f;
    float importScaleY = 1.0f;
    bool flipX = false;
    bool flipY = false;
    ExternalMotionDirection direction = ExternalMotionDirection::CurrentToPrevious;
    float confidence = 0.0f;
    float reprojectionError = 1.0f;
    float zeroMotionError = 1.0f;
    size_t sampledPairs = 0;
    std::string unitLabel;
    std::string summary;
};

// Analyze a representative set of frames from a numbered EXR sequence. The
// range is global across sampled frames; it is never normalized independently
// per frame, which avoids depth breathing in temporal processing.
ExternalDepthCalibrationResult CalibrateExternalDepthSequence(
    const std::filesystem::path& firstFrame,
    size_t maxSampleFrames = 9,
    size_t maxValuesPerFrame = 32768);

// Calibrate channel pair, XY ordering/sign, common unit scale and direction by
// minimizing photometric reprojection error against real adjacent video frames.
// `sourceWidth/sourceHeight` are the full-resolution dimensions of the video and
// EXR motion maps; frame-pair images may be downscaled for fast analysis.
ExternalMotionCalibrationResult CalibrateExternalMotionSequence(
    const std::filesystem::path& firstFrame,
    uint32_t sourceWidth,
    uint32_t sourceHeight,
    const std::vector<MotionCalibrationFramePair>& framePairs,
    size_t maxChannels = 8);

} // namespace video
