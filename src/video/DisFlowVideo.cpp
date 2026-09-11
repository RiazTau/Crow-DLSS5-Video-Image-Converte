#include "DisFlowVideo.h"
#include "AutoDepthRunner.h"
#include "AppPaths.h"
#include "FfmpegProcess.h"
#include "ParallelRows.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

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

float Bilinear(const std::vector<float>& v, uint32_t w, uint32_t h, float x, float y) {
    x = std::clamp(x, 0.0f, static_cast<float>(w - 1u));
    y = std::clamp(y, 0.0f, static_cast<float>(h - 1u));
    const uint32_t x0 = static_cast<uint32_t>(std::floor(x));
    const uint32_t y0 = static_cast<uint32_t>(std::floor(y));
    const uint32_t x1 = std::min(w - 1u, x0 + 1u);
    const uint32_t y1 = std::min(h - 1u, y0 + 1u);
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    const float a = v[static_cast<size_t>(y0) * w + x0];
    const float b = v[static_cast<size_t>(y0) * w + x1];
    const float c = v[static_cast<size_t>(y1) * w + x0];
    const float d = v[static_cast<size_t>(y1) * w + x1];
    const float top = a + (b - a) * tx;
    const float bottom = c + (d - c) * tx;
    return top + (bottom - top) * ty;
}
}

namespace video {

DisFlowVideoSession::DisFlowVideoSession(uint32_t sourceWidth,
                                         uint32_t sourceHeight,
                                         uint32_t analysisWidth,
                                         float sceneCutThreshold,
                                         std::atomic_bool* cancel)
    : _sourceWidth(sourceWidth), _sourceHeight(sourceHeight), _cancel(cancel) {
    if (!_sourceWidth || !_sourceHeight) throw std::runtime_error("DIS flow source dimensions are invalid");
    _analysisWidth = std::clamp(analysisWidth, 128u, _sourceWidth);
    _analysisHeight = std::max(1u, static_cast<uint32_t>(std::lround(
        static_cast<double>(_sourceHeight) * _analysisWidth / static_cast<double>(_sourceWidth))));
    Start(sceneCutThreshold);
}

DisFlowVideoSession::~DisFlowVideoSession() { Stop(); }

void DisFlowVideoSession::Start(float sceneCutThreshold) {
    const auto runtime = autodepth::CheckRuntime();
    if (!runtime.pythonReady) {
        throw std::runtime_error("Temporal DIS runtime is not installed. Run Setup Auto Depth / Temporal first.");
    }
    const auto helper = app::ExecutableDir() / L"auto_depth" / L"dis_flow_video.py";
    if (!std::filesystem::exists(helper)) {
        throw std::runtime_error("DIS optical-flow helper is missing: " + helper.string());
    }
    _logPath = app::ExecutableDir() / L"video" / L"dis-flow-video.log";
    std::filesystem::create_directories(_logPath.parent_path());

    SECURITY_ATTRIBUTES sa{}; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;
    HANDLE childStdin = INVALID_HANDLE_VALUE, childStdout = INVALID_HANDLE_VALUE;
    if (!CreatePipe(&childStdin, &_stdinWrite, &sa, 1u << 20)) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "CreatePipe DIS stdin");
    }
    if (!CreatePipe(&_stdoutRead, &childStdout, &sa, 1u << 20)) {
        CloseHandleSafe(childStdin); CloseHandleSafe(_stdinWrite);
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "CreatePipe DIS stdout");
    }
    SetHandleInformation(_stdinWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(_stdoutRead, HANDLE_FLAG_INHERIT, 0);
    HANDLE log = CreateFileW(_logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) {
        CloseHandleSafe(childStdin); CloseHandleSafe(_stdinWrite); CloseHandleSafe(_stdoutRead); CloseHandleSafe(childStdout);
        throw std::runtime_error("Cannot create DIS optical-flow log");
    }

    std::vector<std::wstring> args = {
        helper.wstring(), L"--server",
        L"--scene-cut", std::to_wstring(std::clamp(sceneCutThreshold, 0.05f, 0.95f))
    };
    std::wstring cmd = JoinCommand(runtime.python, args);
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end()); mutableCmd.push_back(L'\0');
    STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = childStdin; si.hStdOutput = childStdout; si.hStdError = log;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(runtime.python.c_str(), mutableCmd.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        const DWORD e = GetLastError();
        CloseHandleSafe(childStdin); CloseHandleSafe(childStdout); CloseHandleSafe(log);
        CloseHandleSafe(_stdinWrite); CloseHandleSafe(_stdoutRead);
        throw std::system_error(static_cast<int>(e), std::system_category(), "CreateProcess DIS optical flow");
    }
    _process = pi.hProcess; _thread = pi.hThread;
    CloseHandleSafe(childStdin); CloseHandleSafe(childStdout); CloseHandleSafe(log);

    char ready[4]{};
    ReadExact(ready, sizeof(ready), 60000);
    if (std::string(ready, 4) != "RDY1") {
        const auto detail = Tail(_logPath);
        Stop();
        throw std::runtime_error("DIS optical-flow helper did not become ready" + (detail.empty() ? std::string{} : "\n" + detail));
    }
}

void DisFlowVideoSession::WriteExact(const void* data, size_t bytes) {
    const auto* p = static_cast<const uint8_t*>(data);
    size_t done = 0;
    while (done < bytes) {
        if (_cancel && _cancel->load(std::memory_order_relaxed)) throw std::runtime_error("DIS optical flow cancelled");
        DWORD wrote = 0;
        const DWORD part = static_cast<DWORD>(std::min<size_t>(bytes - done, 1u << 20));
        if (!WriteFile(_stdinWrite, p + done, part, &wrote, nullptr) || !wrote) {
            throw std::runtime_error("DIS optical-flow input pipe failed\n" + Tail(_logPath));
        }
        done += wrote;
    }
}

void DisFlowVideoSession::ReadExact(void* data, size_t bytes, DWORD timeoutMs) {
    auto* p = static_cast<uint8_t*>(data);
    size_t done = 0;
    ULONGLONG lastProgress = GetTickCount64();
    while (done < bytes) {
        if (_cancel && _cancel->load(std::memory_order_relaxed)) throw std::runtime_error("DIS optical flow cancelled");
        DWORD available = 0;
        if (!PeekNamedPipe(_stdoutRead, nullptr, 0, nullptr, &available, nullptr)) {
            throw std::runtime_error("DIS optical-flow output pipe failed\n" + Tail(_logPath));
        }
        if (available) {
            DWORD got = 0;
            const DWORD part = static_cast<DWORD>(std::min<size_t>(std::min<size_t>(bytes - done, available), 1u << 20));
            if (!ReadFile(_stdoutRead, p + done, part, &got, nullptr) || !got) {
                throw std::runtime_error("DIS optical-flow output pipe failed\n" + Tail(_logPath));
            }
            done += got;
            lastProgress = GetTickCount64();
            continue;
        }
        if (_process && WaitForSingleObject(_process, 0) == WAIT_OBJECT_0) {
            throw std::runtime_error("DIS optical-flow helper exited unexpectedly\n" + Tail(_logPath));
        }
        if (timeoutMs != INFINITE && GetTickCount64() - lastProgress > timeoutMs) {
            throw std::runtime_error("DIS optical-flow helper timed out\n" + Tail(_logPath));
        }
        Sleep(3);
    }
}

std::vector<uint8_t> DisFlowVideoSession::DownscaleGray(const Rgba8Image& frame) const {
    std::vector<uint8_t> out(static_cast<size_t>(_analysisWidth) * _analysisHeight);
    perf::ParallelForRows(_analysisHeight, 24u, [&](uint32_t rowBegin, uint32_t rowEnd, unsigned) {
        for (uint32_t y = rowBegin; y < rowEnd; ++y) {
        const uint32_t sy = std::min(_sourceHeight - 1u,
            static_cast<uint32_t>((static_cast<uint64_t>(y) * _sourceHeight) / _analysisHeight));
        for (uint32_t x = 0; x < _analysisWidth; ++x) {
            const uint32_t sx = std::min(_sourceWidth - 1u,
                static_cast<uint32_t>((static_cast<uint64_t>(x) * _sourceWidth) / _analysisWidth));
            const auto* p = &frame.pixels[(static_cast<size_t>(sy) * _sourceWidth + sx) * 4u];
            const float yv = 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
            out[static_cast<size_t>(y) * _analysisWidth + x] = static_cast<uint8_t>(std::clamp(std::lround(yv), 0l, 255l));
        }
    }
    });
    return out;
}

TemporalFlowResult DisFlowVideoSession::Process(const Rgba8Image& frame) {
    if (frame.width != _sourceWidth || frame.height != _sourceHeight ||
        frame.pixels.size() != static_cast<size_t>(_sourceWidth) * _sourceHeight * 4u) {
        throw std::runtime_error("DIS optical-flow frame dimensions changed");
    }
    const auto gray = DownscaleGray(frame);
    const char magic[4] = {'F','R','M','1'};
    WriteExact(magic, sizeof(magic));
    WriteExact(&_analysisWidth, sizeof(_analysisWidth));
    WriteExact(&_analysisHeight, sizeof(_analysisHeight));
    WriteExact(gray.data(), gray.size());

    char reply[4]{};
    uint32_t w = 0, h = 0, cut = 0;
    float score = 0.0f;
    ReadExact(reply, sizeof(reply));
    ReadExact(&w, sizeof(w)); ReadExact(&h, sizeof(h));
    ReadExact(&score, sizeof(score)); ReadExact(&cut, sizeof(cut));
    if (std::string(reply, 4) != "FLW1" || w != _analysisWidth || h != _analysisHeight) {
        throw std::runtime_error("DIS optical-flow helper returned an invalid frame header");
    }

    const size_t lowN = static_cast<size_t>(w) * h;
    std::vector<float> lowX(lowN), lowY(lowN), lowC(lowN);
    ReadExact(lowX.data(), lowX.size() * sizeof(float));
    ReadExact(lowY.data(), lowY.size() * sizeof(float));
    ReadExact(lowC.data(), lowC.size() * sizeof(float));

    TemporalFlowResult result;
    result.sceneCutScore = score;
    result.sceneCut = cut != 0;
    const size_t fullN = static_cast<size_t>(_sourceWidth) * _sourceHeight;
    result.motionXY.assign(fullN * 2u, 0.0f);
    result.confidence.assign(fullN, 0.0f);
    if (result.sceneCut) return result;

    const float sx = static_cast<float>(_sourceWidth) / static_cast<float>(_analysisWidth);
    const float sy = static_cast<float>(_sourceHeight) / static_cast<float>(_analysisHeight);
    perf::ParallelForRows(_sourceHeight, 32u, [&](uint32_t rowBegin, uint32_t rowEnd, unsigned) {
        for (uint32_t y = rowBegin; y < rowEnd; ++y) {
        const float ly = (static_cast<float>(y) + 0.5f) / sy - 0.5f;
        for (uint32_t x = 0; x < _sourceWidth; ++x) {
            const float lx = (static_cast<float>(x) + 0.5f) / sx - 0.5f;
            const size_t i = static_cast<size_t>(y) * _sourceWidth + x;
            result.motionXY[i * 2u + 0u] = Bilinear(lowX, w, h, lx, ly) * sx;
            result.motionXY[i * 2u + 1u] = Bilinear(lowY, w, h, lx, ly) * sy;
            result.confidence[i] = std::clamp(Bilinear(lowC, w, h, lx, ly), 0.0f, 1.0f);
        }
    }
    });
    return result;
}

void DisFlowVideoSession::Stop() noexcept {
    if (_stdinWrite != INVALID_HANDLE_VALUE) CancelIoEx(_stdinWrite, nullptr);
    if (_stdoutRead != INVALID_HANDLE_VALUE) CancelIoEx(_stdoutRead, nullptr);
    CloseHandleSafe(_stdinWrite); CloseHandleSafe(_stdoutRead);
    if (_process && _process != INVALID_HANDLE_VALUE) {
        if (WaitForSingleObject(_process, 1500) == WAIT_TIMEOUT) TerminateProcess(_process, 1);
    }
    CloseHandleSafe(_thread); CloseHandleSafe(_process);
}

} // namespace video
