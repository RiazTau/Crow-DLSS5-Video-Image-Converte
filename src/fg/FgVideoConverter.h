#pragma once

#include "ImageWic.h"
#include "video/NvofConfig.h"
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace fg {

enum class FgVideoCodec { H264Nvenc = 0, HevcNvenc = 1, H264Cpu = 2 };

struct FgVideoInfo {
    uint32_t width = 0;
    uint32_t height = 0;
    double fps = 0.0;
    double durationSeconds = 0.0;
    uint64_t totalFrames = 0;
    bool hasAudio = false;
    std::string codecName;
};

struct FgVideoSettings {
    std::filesystem::path input;
    std::filesystem::path output;
    FgVideoCodec codec = FgVideoCodec::H264Nvenc;
    int quality = 18;
    float sceneCutThreshold = 0.28f;
    video::NvofSettings nvof{};
};

struct FgProgress {
    enum class Stage { Preparing, Processing, Finalizing, Completed, Cancelled, Failed };
    Stage stage = Stage::Preparing;
    uint64_t realFrameIndex = 0;
    uint64_t totalRealFrames = 0;
    uint64_t outputFrames = 0;
    double fraction = 0.0;
    double processingFps = 0.0;
    double elapsedSeconds = 0.0;
    std::wstring message;
};

struct FgCallbacks {
    std::function<void(const FgProgress&)> onProgress;
};

FgVideoInfo ProbeFgVideo(const std::filesystem::path& input,
                         uint32_t timeoutMs = 15000,
                         const std::atomic_bool* cancel = nullptr);

void ConvertFgVideo2X(const FgVideoSettings& settings,
                      const FgCallbacks& callbacks,
                      std::atomic_bool& cancelRequested);

} // namespace fg
