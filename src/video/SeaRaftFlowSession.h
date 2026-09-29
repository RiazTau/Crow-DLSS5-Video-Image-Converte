#pragma once
#include "ImageWic.h"
#include "TemporalFlow.h"
#include "VideoConverter.h"
#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace video {

class SeaRaftFlowSession {
public:
    SeaRaftFlowSession(uint32_t sourceWidth,
                       uint32_t sourceHeight,
                       float sceneCutThreshold,
                       const SeaRaftSettings& settings,
                       std::atomic_bool* cancel = nullptr);
    ~SeaRaftFlowSession();
    SeaRaftFlowSession(const SeaRaftFlowSession&) = delete;
    SeaRaftFlowSession& operator=(const SeaRaftFlowSession&) = delete;

    TemporalFlowResult Process(const Rgba8Image& frame);

private:
    void Start(float sceneCutThreshold);
    void WriteExact(const void* data, size_t bytes);
    void ReadExact(void* data, size_t bytes, DWORD timeoutMs = 180000);
    void Stop() noexcept;

    uint32_t _sourceWidth = 0;
    uint32_t _sourceHeight = 0;
    HANDLE _stdinWrite = INVALID_HANDLE_VALUE;
    HANDLE _stdoutRead = INVALID_HANDLE_VALUE;
    HANDLE _process = nullptr;
    HANDLE _thread = nullptr;
    std::filesystem::path _logPath;
    std::atomic_bool* _cancel = nullptr;
    SeaRaftSettings _settings{};
};

} // namespace video
