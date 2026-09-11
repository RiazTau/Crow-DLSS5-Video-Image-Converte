#pragma once
#include "DlssNrRunner.h"
#include "ExternalRenderData.h"
#include "ImageWic.h"
#include "TemporalDenoiser.h"
#include "NvofConfig.h"
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace video {

enum class DepthMode { Zero = 0, AutoDepth = 1, ExternalExr = 2 };
enum class VideoCodec { H264Nvenc = 0, HevcNvenc = 1, H264Cpu = 2 };
enum class TemporalMode {
    LegacyResetEveryFrame = 0,
    DisOpticalFlow = 1,
    CpuFlow = 2,
    ExternalExr = 3,
    NvidiaOpticalFlow = 4
};

struct VideoInfo {
    uint32_t width = 0;
    uint32_t height = 0;
    double fps = 0.0;
    double durationSeconds = 0.0;
    uint64_t totalFrames = 0;
    bool hasAudio = false;
    std::string codecName;
    std::string profile;
    std::string pixelFormat;
};

struct VideoSettings {
    std::filesystem::path input;
    std::filesystem::path output;
    DlssNrSettings dlss;
    DepthMode depthMode = DepthMode::Zero;
    uint32_t autoDepthSize = 518;
    ExternalRenderDataSettings externalData;
    VideoCodec codec = VideoCodec::H264Nvenc;
    int quality = 18; // CQ/CRF style value; lower is higher quality.

    // V0.5 temporal path. Legacy keeps the V0.4.6 reset-every-source-frame behavior.
    TemporalMode temporalMode = TemporalMode::DisOpticalFlow;
    uint32_t flowAnalysisWidth = 480;
    float sceneCutThreshold = 0.28f;
    float mvecScaleX = 1.0f;
    float mvecScaleY = 1.0f;
    bool temporalDepthStabilization = true;
    bool temporalOutputStabilization = false;
    float depthHistoryWeight = 0.20f;
    float outputHistoryWeight = 0.14f;

    // V0.6 full-power denoise path. This runs before DLSSNR so the neural pass
    // receives a temporally coherent, noise-reduced source rather than amplifying
    // frame-random noise into neural shimmer.
    DenoiseMode denoiseMode = DenoiseMode::FullHq;
    float denoiseStrength = 0.72f;
    float denoiseHistoryWeight = 0.76f;
    float denoiseSpatialStrength = 0.22f;
    float denoiseDetailProtection = 0.86f;
    uint32_t denoiseMaxHistory = 24;

    // V0.6.6 NVOF D3D12 tuning. These values are ignored unless temporalMode == NvidiaOpticalFlow.
    NvofSettings nvof;
};

struct VideoProgress {
    enum class Stage { Preparing, Processing, Finalizing, Completed, Cancelled, Failed };
    Stage stage = Stage::Preparing;
    uint64_t frameIndex = 0;
    uint64_t totalFrames = 0;
    double fraction = 0.0;
    double processingFps = 0.0;
    double elapsedSeconds = 0.0;
    double etaSeconds = 0.0;
    std::wstring message;
};

struct VideoCallbacks {
    std::function<void(const VideoProgress&)> onProgress;
    std::function<void(const Rgba8Image&, const Rgba8Image&,
                       const std::vector<float>*, const std::vector<float>*,
                       float, float)> onPreview;
};

VideoInfo ProbeVideo(const std::filesystem::path& input,
                     uint32_t timeoutMs = 15000,
                     const std::atomic_bool* cancel = nullptr);
void ConvertVideo(const VideoSettings& settings,
                  const VideoCallbacks& callbacks,
                  std::atomic_bool& cancelRequested);

} // namespace video
