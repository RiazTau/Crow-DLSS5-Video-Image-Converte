#include "SeaRaftFlowSession.h"
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
    const std::streamoff take = std::min<std::streamoff>(size, 8000);
    in.seekg(-take, std::ios::end);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

float BilinearFlow(const std::vector<float>& flow, uint32_t w, uint32_t h, float x, float y, uint32_t channel) {
    if (!w || !h) return 0.0f;
    x = std::clamp(x, 0.0f, static_cast<float>(w - 1u));
    y = std::clamp(y, 0.0f, static_cast<float>(h - 1u));
    const uint32_t x0 = static_cast<uint32_t>(std::floor(x));
    const uint32_t y0 = static_cast<uint32_t>(std::floor(y));
    const uint32_t x1 = std::min(w - 1u, x0 + 1u);
    const uint32_t y1 = std::min(h - 1u, y0 + 1u);
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    auto at = [&](uint32_t xx, uint32_t yy) { return flow[(static_cast<size_t>(yy) * w + xx) * 2u + channel]; };
    const float a = at(x0,y0), b = at(x1,y0), c = at(x0,y1), d = at(x1,y1);
    return (a + (b-a)*tx) + ((c + (d-c)*tx) - (a + (b-a)*tx))*ty;
}

float BilinearU8(const std::vector<uint8_t>& v, uint32_t w, uint32_t h, float x, float y) {
    if (!w || !h) return 0.0f;
    x = std::clamp(x, 0.0f, static_cast<float>(w - 1u));
    y = std::clamp(y, 0.0f, static_cast<float>(h - 1u));
    const uint32_t x0 = static_cast<uint32_t>(std::floor(x));
    const uint32_t y0 = static_cast<uint32_t>(std::floor(y));
    const uint32_t x1 = std::min(w - 1u, x0 + 1u);
    const uint32_t y1 = std::min(h - 1u, y0 + 1u);
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    auto at = [&](uint32_t xx, uint32_t yy) { return static_cast<float>(v[static_cast<size_t>(yy) * w + xx]) / 255.0f; };
    const float a = at(x0,y0), b = at(x1,y0), c = at(x0,y1), d = at(x1,y1);
    return (a + (b-a)*tx) + ((c + (d-c)*tx) - (a + (b-a)*tx))*ty;
}
}

namespace video {

SeaRaftFlowSession::SeaRaftFlowSession(uint32_t sourceWidth,
                                       uint32_t sourceHeight,
                                       float sceneCutThreshold,
                                       const SeaRaftSettings& settings,
                                       std::atomic_bool* cancel)
    : _sourceWidth(sourceWidth), _sourceHeight(sourceHeight), _cancel(cancel), _settings(settings) {
    if (!_sourceWidth || !_sourceHeight) throw std::runtime_error("SEA-RAFT source dimensions are invalid");
    Start(sceneCutThreshold);
}

SeaRaftFlowSession::~SeaRaftFlowSession() { Stop(); }

void SeaRaftFlowSession::Start(float sceneCutThreshold) {
    const auto root = app::ExecutableDir() / L"sea_raft";
    const auto python = root / L".venv" / L"Scripts" / L"python.exe";
    const auto helper = root / L"sea_raft_video.py";
    const auto repo = root / L"vendor" / L"SEA-RAFT";
    const bool useSmallModel = _settings.model == SeaRaftModel::Small;
    const auto cfg = root / (useSmallModel ? L"spring-S.json" : L"spring-M.json");
    const auto localLegacyModel = root / L"models" / (useSmallModel ? L"sea-raft-spring-S.pth" : L"sea-raft-spring-M.pth");
    const auto localHubModel = root / L"models" / (useSmallModel ? L"spring-S" : L"spring-M");
    if (!std::filesystem::exists(python) || !std::filesystem::exists(helper) ||
        !std::filesystem::exists(repo / L"core" / L"raft.py") || !std::filesystem::exists(cfg)) {
        throw std::runtime_error(
            "SEA-RAFT runtime is not installed or incomplete. Full/Portable Build normally installs it automatically. "
            "Run dist\\sea_raft\\setup_sea_raft.ps1 to repair the runtime, then retry. "
            "The source ZIP keeps PyTorch, upstream SEA-RAFT and model weights external.");
    }

    _logPath = app::ExecutableDir() / L"video" / L"sea-raft-video.log";
    std::filesystem::create_directories(_logPath.parent_path());
    SECURITY_ATTRIBUTES sa{}; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;
    HANDLE childStdin = INVALID_HANDLE_VALUE, childStdout = INVALID_HANDLE_VALUE;
    if (!CreatePipe(&childStdin, &_stdinWrite, &sa, 1u << 20))
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "CreatePipe SEA-RAFT stdin");
    if (!CreatePipe(&_stdoutRead, &childStdout, &sa, 1u << 20)) {
        CloseHandleSafe(childStdin); CloseHandleSafe(_stdinWrite);
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "CreatePipe SEA-RAFT stdout");
    }
    SetHandleInformation(_stdinWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(_stdoutRead, HANDLE_FLAG_INHERIT, 0);
    HANDLE log = CreateFileW(_logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) {
        CloseHandleSafe(childStdin); CloseHandleSafe(childStdout); CloseHandleSafe(_stdinWrite); CloseHandleSafe(_stdoutRead);
        throw std::runtime_error("Cannot create SEA-RAFT log");
    }

    std::vector<std::wstring> args = {
        helper.wstring(), L"--server", L"--repo", repo.wstring(), L"--cfg", cfg.wstring(),
        L"--scene-cut", std::to_wstring(std::clamp(sceneCutThreshold, 0.05f, 0.95f)),
        L"--device", L"cuda",
        L"--scale", std::to_wstring(std::clamp(_settings.inferenceScale, -2, 0)),
        L"--iters", std::to_wstring(std::clamp<uint32_t>(_settings.refinementIterations, 1u, 12u)),
        L"--uncertainty-sensitivity", std::to_wstring(std::clamp(_settings.uncertaintySensitivity, 0.25f, 2.5f))
    };
    if (std::filesystem::exists(localLegacyModel)) {
        args.push_back(L"--model"); args.push_back(localLegacyModel.wstring());
    } else if (std::filesystem::exists(localHubModel / L"model.safetensors")) {
        args.push_back(L"--url"); args.push_back(localHubModel.wstring());
    } else {
        args.push_back(L"--url"); args.push_back(useSmallModel ? L"MemorySlices/Tartan-C-T-TSKH-spring540x960-S" : L"MemorySlices/Tartan-C-T-TSKH-spring540x960-M");
    }
    std::wstring cmd = JoinCommand(python, args);
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end()); mutableCmd.push_back(L'\0');
    STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = childStdin; si.hStdOutput = childStdout; si.hStdError = log;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(python.c_str(), mutableCmd.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, root.c_str(), &si, &pi)) {
        const DWORD e = GetLastError();
        CloseHandleSafe(childStdin); CloseHandleSafe(childStdout); CloseHandleSafe(log);
        CloseHandleSafe(_stdinWrite); CloseHandleSafe(_stdoutRead);
        throw std::system_error(static_cast<int>(e), std::system_category(), "CreateProcess SEA-RAFT");
    }
    _process = pi.hProcess; _thread = pi.hThread;
    CloseHandleSafe(childStdin); CloseHandleSafe(childStdout); CloseHandleSafe(log);

    char ready[4]{};
    ReadExact(ready, sizeof(ready), 300000);
    if (std::string(ready, 4) != "SRD1") {
        const auto detail = Tail(_logPath); Stop();
        throw std::runtime_error("SEA-RAFT helper did not become ready" + (detail.empty() ? std::string{} : "\n" + detail));
    }
}

void SeaRaftFlowSession::WriteExact(const void* data, size_t bytes) {
    const auto* p = static_cast<const uint8_t*>(data);
    size_t done = 0;
    while (done < bytes) {
        if (_cancel && _cancel->load(std::memory_order_relaxed)) throw std::runtime_error("SEA-RAFT cancelled");
        DWORD wrote = 0;
        const DWORD part = static_cast<DWORD>(std::min<size_t>(bytes - done, 1u << 20));
        if (!WriteFile(_stdinWrite, p + done, part, &wrote, nullptr) || !wrote)
            throw std::runtime_error("SEA-RAFT input pipe failed\n" + Tail(_logPath));
        done += wrote;
    }
}

void SeaRaftFlowSession::ReadExact(void* data, size_t bytes, DWORD timeoutMs) {
    auto* p = static_cast<uint8_t*>(data);
    size_t done = 0;
    ULONGLONG lastProgress = GetTickCount64();
    while (done < bytes) {
        if (_cancel && _cancel->load(std::memory_order_relaxed)) throw std::runtime_error("SEA-RAFT cancelled");
        DWORD available = 0;
        if (!PeekNamedPipe(_stdoutRead, nullptr, 0, nullptr, &available, nullptr))
            throw std::runtime_error("SEA-RAFT output pipe failed\n" + Tail(_logPath));
        if (available) {
            DWORD got = 0;
            const DWORD part = static_cast<DWORD>(std::min<size_t>(std::min<size_t>(bytes - done, available), 1u << 20));
            if (!ReadFile(_stdoutRead, p + done, part, &got, nullptr) || !got)
                throw std::runtime_error("SEA-RAFT output pipe failed\n" + Tail(_logPath));
            done += got; lastProgress = GetTickCount64(); continue;
        }
        if (_process && WaitForSingleObject(_process, 0) == WAIT_OBJECT_0)
            throw std::runtime_error("SEA-RAFT helper exited unexpectedly\n" + Tail(_logPath));
        if (timeoutMs != INFINITE && GetTickCount64() - lastProgress > timeoutMs)
            throw std::runtime_error("SEA-RAFT helper timed out\n" + Tail(_logPath));
        Sleep(3);
    }
}

TemporalFlowResult SeaRaftFlowSession::Process(const Rgba8Image& frame) {
    if (frame.width != _sourceWidth || frame.height != _sourceHeight ||
        frame.pixels.size() != static_cast<size_t>(_sourceWidth) * _sourceHeight * 4u)
        throw std::runtime_error("Invalid frame supplied to SEA-RAFT");

    const char magic[4] = {'F','R','M','1'};
    WriteExact(magic, 4); WriteExact(&frame.width, 4); WriteExact(&frame.height, 4);
    WriteExact(frame.pixels.data(), frame.pixels.size());

    char reply[4]{};
    uint32_t sourceW=0, sourceH=0, gridW=0, gridH=0, flags=0;
    float sceneScore=0.0f;
    ReadExact(reply, 4); ReadExact(&sourceW, 4); ReadExact(&sourceH, 4);
    ReadExact(&gridW, 4); ReadExact(&gridH, 4); ReadExact(&sceneScore, 4); ReadExact(&flags, 4);
    if (std::string(reply,4) != "SRF1" || sourceW != _sourceWidth || sourceH != _sourceHeight)
        throw std::runtime_error("SEA-RAFT helper returned an invalid frame header");

    TemporalFlowResult out;
    out.sceneCutScore = sceneScore;
    out.sceneCut = (flags & 1u) != 0u;
    const size_t n = static_cast<size_t>(_sourceWidth) * _sourceHeight;
    out.motionXY.assign(n * 2u, 0.0f);
    out.rawMotionXY.assign(n * 2u, 0.0f);
    out.confidence.assign(n, out.sceneCut ? 0.0f : 1.0f);
    out.historyVisibility.assign(n, out.sceneCut ? 0.0f : 1.0f);
    out.occlusionProbability.assign(n, 0.0f);
    out.disocclusionProbability.assign(n, out.sceneCut ? 1.0f : 0.0f);
    out.motionUncertainty.assign(n, out.sceneCut ? 1.0f : 0.0f);
    if (!gridW || !gridH || out.sceneCut) {
        out.meanHistoryVisibility = 0.0f;
        out.disoccludedFraction = 1.0f;
        out.highUncertaintyFraction = 1.0f;
        return out;
    }

    const size_t gridN = static_cast<size_t>(gridW) * gridH;
    std::vector<float> gridFlow(gridN * 2u);
    std::vector<uint8_t> gridUncertainty(gridN);
    ReadExact(gridFlow.data(), gridFlow.size() * sizeof(float));
    ReadExact(gridUncertainty.data(), gridUncertainty.size());

    std::vector<double> visSums(perf::WorkerCountForRows(_sourceHeight, 48u), 0.0);
    std::vector<uint64_t> disoccCounts(visSums.size(), 0u), highUncertaintyCounts(visSums.size(), 0u);
    perf::ParallelForRows(_sourceHeight, 48u, [&](uint32_t y0, uint32_t y1, unsigned worker) {
        double visSum=0.0; uint64_t disocc=0, highU=0;
        for (uint32_t y=y0; y<y1; ++y) {
            const float gy = (static_cast<float>(y) + 0.5f) * static_cast<float>(gridH) / static_cast<float>(_sourceHeight) - 0.5f;
            for (uint32_t x=0; x<_sourceWidth; ++x) {
                const float gx = (static_cast<float>(x) + 0.5f) * static_cast<float>(gridW) / static_cast<float>(_sourceWidth) - 0.5f;
                const float vx = BilinearFlow(gridFlow, gridW, gridH, gx, gy, 0u);
                const float vy = BilinearFlow(gridFlow, gridW, gridH, gx, gy, 1u);
                const float uncertainty = std::clamp(BilinearU8(gridUncertainty, gridW, gridH, gx, gy), 0.0f, 1.0f);
                const size_t i = static_cast<size_t>(y) * _sourceWidth + x;
                out.motionXY[i*2u+0u]=vx; out.motionXY[i*2u+1u]=vy;
                out.rawMotionXY[i*2u+0u]=vx; out.rawMotionXY[i*2u+1u]=vy;
                float confidence = std::clamp((1.0f - 0.88f*uncertainty) * std::clamp(_settings.neuralFlowTrust, 0.50f, 1.50f), 0.04f, 1.0f);
                float visibility = std::clamp(1.0f - 0.82f*uncertainty, 0.0f, 1.0f);
                float disocclusion = 0.0f;
                const float px = static_cast<float>(x) + vx, py = static_cast<float>(y) + vy;
                if (px < 0.0f || py < 0.0f || px > static_cast<float>(_sourceWidth-1u) || py > static_cast<float>(_sourceHeight-1u)) {
                    disocclusion = 1.0f; visibility = 0.0f; confidence = std::min(confidence, 0.05f);
                }
                out.confidence[i]=confidence;
                out.historyVisibility[i]=visibility;
                out.disocclusionProbability[i]=disocclusion;
                out.motionUncertainty[i]=uncertainty;
                visSum += visibility;
                if (disocclusion >= 0.5f) ++disocc;
                if (uncertainty >= 0.65f) ++highU;
            }
        }
        visSums[worker]=visSum; disoccCounts[worker]=disocc; highUncertaintyCounts[worker]=highU;
    });
    double vis=0.0; uint64_t disocc=0, highU=0;
    for (size_t i=0;i<visSums.size();++i) { vis+=visSums[i]; disocc+=disoccCounts[i]; highU+=highUncertaintyCounts[i]; }
    out.meanHistoryVisibility = n ? static_cast<float>(vis/static_cast<double>(n)) : 0.0f;
    out.disoccludedFraction = n ? static_cast<float>(disocc)/static_cast<float>(n) : 0.0f;
    out.highUncertaintyFraction = n ? static_cast<float>(highU)/static_cast<float>(n) : 0.0f;
    return out;
}

void SeaRaftFlowSession::Stop() noexcept {
    if (_stdinWrite != INVALID_HANDLE_VALUE) CancelIoEx(_stdinWrite, nullptr);
    if (_stdoutRead != INVALID_HANDLE_VALUE) CancelIoEx(_stdoutRead, nullptr);
    CloseHandleSafe(_stdinWrite); CloseHandleSafe(_stdoutRead);
    if (_process && _process != INVALID_HANDLE_VALUE) {
        if (WaitForSingleObject(_process, 2000) == WAIT_TIMEOUT) TerminateProcess(_process, 1);
    }
    CloseHandleSafe(_thread); CloseHandleSafe(_process);
}

} // namespace video
