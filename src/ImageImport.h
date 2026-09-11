#pragma once
#include "ImageWic.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

enum class ExrToneMap {
    Clamp = 0,
    Reinhard = 1,
    AcesFitted = 2,
};

struct ImageImportSettings {
    std::string exrLayer;
    float exposureEv = 0.0f;
    ExrToneMap toneMap = ExrToneMap::AcesFitted;
    bool srgbEncode = true;
};

struct ImportedImage {
    Rgba8Image display;
    bool fromExr = false;
    std::string selectedLayer;
    std::vector<std::string> exrLayers;
};

struct DepthImportSettings {
    std::string exrLayer;
    std::string exrChannel;       // exact EXR channel, e.g. ViewLayer.Depth.Z
    int channel = 0;              // 0=R,1=G,2=B,3=A,4=Luminance
    bool autoNormalize = true;
    bool inverseDepth = true;     // near=white, sets DLSSNR.DepthInverted=1
    float scale = 1.0f;
    float offset = 0.0f;
};


struct FloatChannelMap {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<float> values;
};

struct DepthMap {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<float> values;
    bool inverseDepth = true;
};

bool IsExrPath(const std::filesystem::path& path);
std::vector<std::string> ListExrLayers(const std::filesystem::path& path);
std::vector<std::string> ListExrChannels(const std::filesystem::path& path);
FloatChannelMap LoadExrFloatChannel(const std::filesystem::path& path, const std::string& channelName);
ImportedImage LoadImageForDlss(const std::filesystem::path& path, const ImageImportSettings& settings);
DepthMap LoadDepthMap(const std::filesystem::path& path, const DepthImportSettings& settings,
                      uint32_t targetWidth, uint32_t targetHeight);
void SaveOutputImage(const std::filesystem::path& path, const Rgba8Image& image);
