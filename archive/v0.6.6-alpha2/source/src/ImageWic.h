#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>

struct Rgba8Image {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels;
};

Rgba8Image LoadImageRgba8(const std::filesystem::path& path);
void SavePngRgba8(const std::filesystem::path& path, const Rgba8Image& image);
