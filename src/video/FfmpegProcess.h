#pragma once
#include <Windows.h>
#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>
#include <atomic>

namespace video {

std::wstring QuoteArg(const std::wstring& value);
std::wstring JoinCommand(const std::filesystem::path& exe, const std::vector<std::wstring>& args);
std::filesystem::path FindFfmpeg();
std::filesystem::path FindFfprobe();
std::string RunCapture(const std::filesystem::path& exe,
                       const std::vector<std::wstring>& args,
                       DWORD timeoutMs = 15000,
                       const std::atomic_bool* cancel = nullptr);

class PipeReaderProcess {
public:
    PipeReaderProcess(const std::filesystem::path& exe,
                      const std::vector<std::wstring>& args,
                      const std::filesystem::path& logPath);
    ~PipeReaderProcess();
    PipeReaderProcess(const PipeReaderProcess&) = delete;
    PipeReaderProcess& operator=(const PipeReaderProcess&) = delete;

    bool ReadExact(void* dst, size_t bytes,
                   const std::atomic_bool* cancel = nullptr,
                   DWORD stallTimeoutMs = 30000);
    DWORD Wait(DWORD timeoutMs = 30000);
    void Terminate() noexcept;

private:
    HANDLE _read = INVALID_HANDLE_VALUE;
    HANDLE _process = nullptr;
    HANDLE _thread = nullptr;
};

class PipeWriterProcess {
public:
    PipeWriterProcess(const std::filesystem::path& exe,
                      const std::vector<std::wstring>& args,
                      const std::filesystem::path& logPath);
    ~PipeWriterProcess();
    PipeWriterProcess(const PipeWriterProcess&) = delete;
    PipeWriterProcess& operator=(const PipeWriterProcess&) = delete;

    bool WriteExact(const void* src, size_t bytes,
                    const std::atomic_bool* cancel = nullptr);
    void CloseInput() noexcept;
    DWORD Wait(DWORD timeoutMs = 60000);
    void Terminate() noexcept;

private:
    HANDLE _write = INVALID_HANDLE_VALUE;
    HANDLE _process = nullptr;
    HANDLE _thread = nullptr;
};

} // namespace video
