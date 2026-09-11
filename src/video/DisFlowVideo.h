#pragma once
#include "ImageWic.h"
#include "TemporalFlow.h"
#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <filesystem>

namespace video {

class DisFlowVideoSession {
public:
    DisFlowVideoSession(uint32_t sourceWidth,
                        uint32_t sourceHeight,
                        uint32_t analysisWidth,
                        float sceneCutThreshold,
                        std::atomic_bool* cancel = nullptr);
    ~DisFlowVideoSession();
    DisFlowVideoSession(const DisFlowVideoSession&) = delete;
    DisFlowVideoSession& operator=(const DisFlowVideoSession&) = delete;

    TemporalFlowResult Process(const Rgba8Image& frame);

private:
    void Start(float sceneCutThreshold);
    void WriteExact(const void* data, size_t bytes);
    void ReadExact(void* data, size_t bytes, DWORD timeoutMs = 30000);
    void Stop() noexcept;
    std::vector<uint8_t> DownscaleGray(const Rgba8Image& frame) const;

    uint32_t _sourceWidth = 0;
    uint32_t _sourceHeight = 0;
    uint32_t _analysisWidth = 0;
    uint32_t _analysisHeight = 0;
    HANDLE _stdinWrite = INVALID_HANDLE_VALUE;
    HANDLE _stdoutRead = INVALID_HANDLE_VALUE;
    HANDLE _process = nullptr;
    HANDLE _thread = nullptr;
    std::filesystem::path _logPath;
    std::atomic_bool* _cancel = nullptr;
};

} // namespace video
