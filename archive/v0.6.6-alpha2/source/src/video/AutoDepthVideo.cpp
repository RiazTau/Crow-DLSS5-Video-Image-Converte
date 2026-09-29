#include "AutoDepthVideo.h"
#include "AutoDepthRunner.h"
#include "AppPaths.h"
#include "ModelBootstrap.h"
#include "FfmpegProcess.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include <system_error>

namespace {
void CloseHandleSafe(HANDLE& h) noexcept {
    if (h && h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
        h = INVALID_HANDLE_VALUE;
    }
}

std::string Tail(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    in.seekg(0, std::ios::end);
    const auto end = in.tellg();
    const std::streamoff size = end == std::streampos(-1) ? 0 : static_cast<std::streamoff>(end);
    const std::streamoff take = std::min<std::streamoff>(size, 6000);
    in.seekg(-take, std::ios::end);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
}

namespace video {

AutoDepthVideoSession::AutoDepthVideoSession(uint32_t inferenceSize, std::atomic_bool* cancel) : _cancel(cancel) { Start(inferenceSize); }
AutoDepthVideoSession::~AutoDepthVideoSession() { Stop(); }

void AutoDepthVideoSession::Start(uint32_t inferenceSize) {
    auto status = autodepth::CheckRuntime();
    if (!status.pythonReady || !status.helperReady) {
        throw std::runtime_error("Auto Depth runtime is not installed. Run Setup Auto Depth first.");
    }
    auto model = status.modelReady ? status.model : models::EnsureDepthAnythingV2();
    const auto helper = app::ExecutableDir() / L"auto_depth" / L"auto_depth_video.py";
    if (!std::filesystem::exists(helper)) {
        throw std::runtime_error("Auto Depth video helper is missing: " + helper.string());
    }
    _logPath = app::ExecutableDir() / L"video" / L"auto-depth-video.log";
    std::filesystem::create_directories(_logPath.parent_path());

    SECURITY_ATTRIBUTES sa{}; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;
    HANDLE childStdin = INVALID_HANDLE_VALUE, childStdout = INVALID_HANDLE_VALUE;
    if (!CreatePipe(&childStdin, &_stdinWrite, &sa, 1u << 20)) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "CreatePipe AutoDepth stdin");
    }
    if (!CreatePipe(&_stdoutRead, &childStdout, &sa, 1u << 20)) {
        CloseHandleSafe(childStdin); CloseHandleSafe(_stdinWrite);
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "CreatePipe AutoDepth stdout");
    }
    SetHandleInformation(_stdinWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(_stdoutRead, HANDLE_FLAG_INHERIT, 0);
    HANDLE log = CreateFileW(_logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) {
        CloseHandleSafe(childStdin); CloseHandleSafe(_stdinWrite); CloseHandleSafe(_stdoutRead); CloseHandleSafe(childStdout);
        throw std::runtime_error("Cannot create Auto Depth video log");
    }

    std::vector<std::wstring> args = {
        helper.wstring(), L"--model", model.wstring(), L"--size",
        std::to_wstring(std::clamp<uint32_t>(inferenceSize, 280, 1036)), L"--server"
    };
    std::wstring cmd = JoinCommand(status.python, args);
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end()); mutableCmd.push_back(L'\0');
    STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = childStdin; si.hStdOutput = childStdout; si.hStdError = log;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(status.python.c_str(), mutableCmd.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        const DWORD e = GetLastError();
        CloseHandleSafe(childStdin); CloseHandleSafe(childStdout); CloseHandleSafe(log);
        if (_stdinWrite != INVALID_HANDLE_VALUE) CancelIoEx(_stdinWrite, nullptr);
    if (_stdoutRead != INVALID_HANDLE_VALUE) CancelIoEx(_stdoutRead, nullptr);
    CloseHandleSafe(_stdinWrite); CloseHandleSafe(_stdoutRead);
        throw std::system_error(static_cast<int>(e), std::system_category(), "CreateProcess AutoDepth video");
    }
    _process = pi.hProcess; _thread = pi.hThread;
    CloseHandleSafe(childStdin); CloseHandleSafe(childStdout); CloseHandleSafe(log);

    char ready[4]{};
    ReadExact(ready, sizeof(ready), 120000);
    if (std::string(ready, 4) != "RDY1") {
        const auto detail = Tail(_logPath);
        Stop();
        throw std::runtime_error("Auto Depth video helper did not become ready" + (detail.empty() ? std::string{} : "\n" + detail));
    }
}

void AutoDepthVideoSession::WriteExact(const void* data, size_t bytes) {
    const auto* p = static_cast<const uint8_t*>(data);
    size_t done = 0;
    while (done < bytes) {
        DWORD wrote = 0;
        DWORD part = static_cast<DWORD>(std::min<size_t>(bytes - done, 1u << 20));
        if (_cancel && _cancel->load(std::memory_order_relaxed)) throw std::runtime_error("Auto Depth cancelled");
        if (!WriteFile(_stdinWrite, p + done, part, &wrote, nullptr) || !wrote) {
            const DWORD err = GetLastError();
            if (err == ERROR_OPERATION_ABORTED && _cancel && _cancel->load(std::memory_order_relaxed))
                throw std::runtime_error("Auto Depth cancelled");
            throw std::runtime_error("Auto Depth video helper input pipe failed\n" + Tail(_logPath));
        }
        done += wrote;
    }
}

void AutoDepthVideoSession::ReadExact(void* data, size_t bytes, DWORD timeoutMs) {
    auto* p = static_cast<uint8_t*>(data);
    size_t done = 0;
    ULONGLONG lastProgress = GetTickCount64();
    while (done < bytes) {
        if (_cancel && _cancel->load(std::memory_order_relaxed)) throw std::runtime_error("Auto Depth cancelled");

        DWORD available = 0;
        if (!PeekNamedPipe(_stdoutRead, nullptr, 0, nullptr, &available, nullptr)) {
            throw std::runtime_error("Auto Depth video helper output pipe failed\n" + Tail(_logPath));
        }
        if (available) {
            DWORD got = 0;
            const DWORD part = static_cast<DWORD>(std::min<size_t>(std::min<size_t>(bytes - done, available), 1u << 20));
            if (!ReadFile(_stdoutRead, p + done, part, &got, nullptr) || !got) {
                const DWORD err = GetLastError();
                if (err == ERROR_OPERATION_ABORTED && _cancel && _cancel->load(std::memory_order_relaxed))
                    throw std::runtime_error("Auto Depth cancelled");
                throw std::runtime_error("Auto Depth video helper output pipe failed\n" + Tail(_logPath));
            }
            done += got;
            lastProgress = GetTickCount64();
            continue;
        }
        if (_process && WaitForSingleObject(_process, 0) == WAIT_OBJECT_0) {
            throw std::runtime_error("Auto Depth helper exited unexpectedly\n" + Tail(_logPath));
        }
        if (timeoutMs != INFINITE && GetTickCount64() - lastProgress > timeoutMs) {
            throw std::runtime_error("Auto Depth helper timed out after " + std::to_string(timeoutMs / 1000) + " seconds\n" + Tail(_logPath));
        }
        Sleep(5);
    }
}

std::vector<float> AutoDepthVideoSession::Process(const Rgba8Image& frame) {
    if (!frame.width || !frame.height || frame.pixels.size() != static_cast<size_t>(frame.width) * frame.height * 4u) {
        throw std::runtime_error("Invalid video frame for Auto Depth");
    }
    const char magic[4] = {'F','R','M','1'};
    WriteExact(magic, sizeof(magic));
    WriteExact(&frame.width, sizeof(frame.width));
    WriteExact(&frame.height, sizeof(frame.height));
    WriteExact(frame.pixels.data(), frame.pixels.size());

    char reply[4]{}; uint32_t w = 0, h = 0;
    ReadExact(reply, sizeof(reply)); ReadExact(&w, sizeof(w)); ReadExact(&h, sizeof(h));
    if (std::string(reply, 4) != "DEP1" || w != frame.width || h != frame.height) {
        throw std::runtime_error("Auto Depth video helper returned an invalid frame header");
    }
    std::vector<float> depth(static_cast<size_t>(w) * h);
    ReadExact(depth.data(), depth.size() * sizeof(float));
    return depth;
}

void AutoDepthVideoSession::Stop() noexcept {
    // Closing stdin is sufficient: the Python server treats EOF as a clean shutdown.
    // Do not synchronously WriteFile("QUIT") here because a wedged helper could make
    // destructor/shutdown itself block on a full pipe.
    if (_stdinWrite != INVALID_HANDLE_VALUE) CancelIoEx(_stdinWrite, nullptr);
    if (_stdoutRead != INVALID_HANDLE_VALUE) CancelIoEx(_stdoutRead, nullptr);
    CloseHandleSafe(_stdinWrite); CloseHandleSafe(_stdoutRead);
    if (_process && _process != INVALID_HANDLE_VALUE) {
        if (WaitForSingleObject(_process, 1500) == WAIT_TIMEOUT) TerminateProcess(_process, 1);
    }
    CloseHandleSafe(_thread); CloseHandleSafe(_process);
}

} // namespace video
