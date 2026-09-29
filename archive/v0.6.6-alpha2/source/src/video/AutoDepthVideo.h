#pragma once
#include "ImageWic.h"
#include <Windows.h>
#include <filesystem>
#include <vector>
#include <atomic>
#include <cstdint>

namespace video {

class AutoDepthVideoSession {
public:
    explicit AutoDepthVideoSession(uint32_t inferenceSize = 518, std::atomic_bool* cancel = nullptr);
    ~AutoDepthVideoSession();
    AutoDepthVideoSession(const AutoDepthVideoSession&) = delete;
    AutoDepthVideoSession& operator=(const AutoDepthVideoSession&) = delete;

    std::vector<float> Process(const Rgba8Image& frame);

private:
    void Start(uint32_t inferenceSize);
    void WriteExact(const void* data, size_t bytes);
    void ReadExact(void* data, size_t bytes, DWORD timeoutMs = 30000);
    void Stop() noexcept;

    HANDLE _stdinWrite = INVALID_HANDLE_VALUE;
    HANDLE _stdoutRead = INVALID_HANDLE_VALUE;
    HANDLE _process = nullptr;
    HANDLE _thread = nullptr;
    std::filesystem::path _logPath;
    std::atomic_bool* _cancel = nullptr;
};

} // namespace video
