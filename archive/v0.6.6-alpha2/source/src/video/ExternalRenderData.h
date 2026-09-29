#pragma once
#include "ExternalSequence.h"
#include "ImageWic.h"
#include "TemporalFlow.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace video {

enum class ExternalDepthMapping { FixedRange = 0, Raw01 = 1 };
enum class ExternalMotionDirection { CurrentToPrevious = 0, PreviousToCurrent = 1 };

struct ExternalDepthSettings {
    std::filesystem::path firstFrame;
    std::string channel;
    ExternalDepthMapping mapping = ExternalDepthMapping::FixedRange;
    float nearValue = 0.1f;
    float farValue = 100.0f;
    // Mirrors DLSSNR.DepthInverted: true means larger normalized depth is nearer.
    bool depthInverted = true;
};

struct ExternalMotionSettings {
    std::filesystem::path firstFrame;
    std::string xChannel;
    std::string yChannel;
    // Imported channels are converted to internal current -> previous motion in source-resolution pixels.
    float importScaleX = 1.0f;
    float importScaleY = 1.0f;
    bool flipX = false;
    bool flipY = false;
    // CurrentToPrevious consumes motion frame N for video frame N.
    // PreviousToCurrent consumes motion frame N-1 and numerically inverts the
    // forward field to the internal current->previous convention.
    ExternalMotionDirection direction = ExternalMotionDirection::CurrentToPrevious;
};

struct ExternalRenderDataSettings {
    ExternalDepthSettings depth;
    ExternalMotionSettings motion;
};

struct ExternalDataProbe {
    std::vector<std::string> channels;
    std::wstring sequenceDescription;
};

ExternalDataProbe ProbeExternalExr(const std::filesystem::path& firstFrame);
std::string AutoSelectDepthChannel(const std::vector<std::string>& channels);
std::string AutoSelectMotionXChannel(const std::vector<std::string>& channels);
std::string AutoSelectMotionYChannel(const std::vector<std::string>& channels);

class ExternalRenderDataReader {
public:
    ExternalRenderDataReader(ExternalRenderDataSettings settings,
                             uint32_t width,
                             uint32_t height,
                             uint64_t expectedFrames,
                             bool needDepth,
                             bool needMotion);

    void Validate() const;
    std::vector<float> LoadDepth(uint64_t oneBasedVideoFrame) const;
    TemporalFlowResult LoadMotion(uint64_t oneBasedVideoFrame,
                                  const Rgba8Image* previous,
                                  const Rgba8Image& current,
                                  uint32_t sceneAnalysisWidth,
                                  float sceneCutThreshold) const;

private:
    std::filesystem::path RequireDepthPath(uint64_t oneBasedVideoFrame) const;
    std::filesystem::path RequireMotionPath(uint64_t oneBasedVideoFrame) const;

    ExternalRenderDataSettings _settings;
    uint32_t _width = 0;
    uint32_t _height = 0;
    uint64_t _expectedFrames = 0;
    bool _needDepth = false;
    bool _needMotion = false;
    ExrSequencePattern _depthPattern{};
    ExrSequencePattern _motionPattern{};
};

} // namespace video
