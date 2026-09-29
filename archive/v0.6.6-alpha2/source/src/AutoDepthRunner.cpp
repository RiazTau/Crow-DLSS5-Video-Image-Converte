#include "AutoDepthRunner.h"
#include "AppPaths.h"
#include "ImageWic.h"
#include "ModelBootstrap.h"
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::wstring Quote(const std::filesystem::path& p) {
    std::wstring s = p.wstring();
    std::wstring out = L"\"";
    for (wchar_t c : s) {
        if (c == L'\"') out += L"\\\"";
        else out += c;
    }
    out += L"\"";
    return out;
}


std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss; ss << in.rdbuf();
    auto text = ss.str();
    if (text.size() > 6000) text = text.substr(text.size() - 6000);
    return text;
}

void RunProcess(const std::filesystem::path& exe, const std::wstring& args, const std::filesystem::path& logPath) {
    std::wstring cmd = Quote(exe) + L" " + args;
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');

    SECURITY_ATTRIBUTES sa{}; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;
    HANDLE log = CreateFileW(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create Auto Depth helper log");

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = log; si.hStdError = log; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        const DWORD err = GetLastError(); CloseHandle(log);
        throw std::runtime_error("CreateProcessW failed for Auto Depth helper, Win32=" + std::to_string(err));
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(log);
    if (code != 0) {
        const auto details = ReadTextFile(logPath);
        throw std::runtime_error("Auto Depth helper failed with exit code " + std::to_string(code) +
                                 (details.empty() ? std::string{} : std::string("\n\n") + details));
    }
}

std::filesystem::path TempDir() {
    wchar_t base[MAX_PATH]{};
    const DWORD n = GetTempPathW(MAX_PATH, base);
    if (!n || n >= MAX_PATH) throw std::runtime_error("GetTempPathW failed");
    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    auto dir = std::filesystem::path(base) / (L"crow-dlss5-autodepth-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(stamp));
    std::filesystem::create_directories(dir);
    return dir;
}

DepthMap ReadDepthBinary(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Auto Depth output was not produced");
    char magic[4]{};
    uint32_t w = 0, h = 0;
    in.read(magic, 4);
    in.read(reinterpret_cast<char*>(&w), sizeof(w));
    in.read(reinterpret_cast<char*>(&h), sizeof(h));
    if (!in || std::string(magic, 4) != "DAV2" || !w || !h) {
        throw std::runtime_error("Invalid Auto Depth helper output");
    }
    DepthMap out;
    out.width = w;
    out.height = h;
    out.inverseDepth = true;
    out.values.resize(static_cast<size_t>(w) * h);
    in.read(reinterpret_cast<char*>(out.values.data()), static_cast<std::streamsize>(out.values.size() * sizeof(float)));
    if (!in) throw std::runtime_error("Auto Depth output is truncated");
    return out;
}
}

namespace autodepth {
RuntimeStatus CheckRuntime() {
    RuntimeStatus s;
    const auto root = app::ExecutableDir() / L"auto_depth";
    s.python = root / L".venv" / L"Scripts" / L"python.exe";
    s.helper = root / L"auto_depth.py";
    s.model = app::DefaultDepthModel();
    s.pythonReady = std::filesystem::exists(s.python);
    s.helperReady = std::filesystem::exists(s.helper);
    const auto model = models::CheckDepthAnythingV2();
    s.modelReady = model.present && model.hashOk;
    return s;
}

std::filesystem::path SetupScript() {
    return app::ExecutableDir() / L"auto_depth" / L"setup_auto_depth.ps1";
}

DepthMap Generate(const Rgba8Image& input, uint32_t inferenceSize) {
    if (!input.width || !input.height || input.pixels.empty()) throw std::runtime_error("Auto Depth input is empty");
    auto status = CheckRuntime();
    if (!status.pythonReady || !status.helperReady) {
        throw std::runtime_error("Auto Depth runtime is not installed. Click 'Setup Auto Depth...' once, then retry.");
    }
    const auto model = status.modelReady ? status.model : models::EnsureDepthAnythingV2();
    const auto dir = TempDir();
    const auto inputPng = dir / L"input.png";
    const auto outputBin = dir / L"depth.bin";
    const auto logPath = dir / L"auto_depth.log";
    try {
        SavePngRgba8(inputPng, input);
        std::wstringstream args;
        args << Quote(status.helper)
             << L" --model " << Quote(model)
             << L" --input " << Quote(inputPng)
             << L" --output " << Quote(outputBin)
             << L" --size " << std::clamp<uint32_t>(inferenceSize, 280, 1036);
        RunProcess(status.python, args.str(), logPath);
        auto depth = ReadDepthBinary(outputBin);
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        return depth;
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        throw;
    }
}
}
