#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

namespace video {

struct ExrSequencePattern {
    std::filesystem::path directory;
    std::wstring prefix;
    std::wstring suffix;
    uint64_t firstNumber = 0;
    unsigned padding = 0;

    std::filesystem::path FramePath(uint64_t zeroBasedFrame) const;
    std::wstring Description() const;
};

ExrSequencePattern ParseExrSequencePattern(const std::filesystem::path& firstFrame);

} // namespace video
