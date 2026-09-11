#include "AppPaths.h"
#include "D3D12Context.h"
#include "DlssNrRunner.h"
#include "RuntimeCompat.h"
#include <Windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(std::max(0, n)), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::string Sha256(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "unavailable";
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectBytes = 0, hashBytes = 0, cb = 0;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return "unavailable";
    auto closeAlg = [&] { if (alg) BCryptCloseAlgorithmProvider(alg, 0); };
    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectBytes), sizeof(objectBytes), &cb, 0) < 0 ||
        BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashBytes), sizeof(hashBytes), &cb, 0) < 0) {
        closeAlg(); return "unavailable";
    }
    std::vector<UCHAR> object(objectBytes), digest(hashBytes);
    if (BCryptCreateHash(alg, &hash, object.data(), objectBytes, nullptr, 0, 0) < 0) { closeAlg(); return "unavailable"; }
    std::array<char, 1 << 16> buffer{};
    while (in) {
        in.read(buffer.data(), buffer.size());
        const auto got = in.gcount();
        if (got > 0 && BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(got), 0) < 0) {
            BCryptDestroyHash(hash); closeAlg(); return "unavailable";
        }
    }
    if (BCryptFinishHash(hash, digest.data(), hashBytes, 0) < 0) {
        BCryptDestroyHash(hash); closeAlg(); return "unavailable";
    }
    BCryptDestroyHash(hash); closeAlg();
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (auto b : digest) out << std::setw(2) << static_cast<unsigned>(b);
    return out.str();
}

Rgba8Image MakeProbeImage() {
    Rgba8Image image;
    image.width = 256; image.height = 256;
    image.pixels.resize(static_cast<size_t>(image.width) * image.height * 4u);
    for (uint32_t y = 0; y < image.height; ++y) {
        for (uint32_t x = 0; x < image.width; ++x) {
            const size_t i = (static_cast<size_t>(y) * image.width + x) * 4u;
            image.pixels[i + 0] = static_cast<uint8_t>(x);
            image.pixels[i + 1] = static_cast<uint8_t>(y);
            image.pixels[i + 2] = static_cast<uint8_t>((x ^ y) & 0xffu);
            image.pixels[i + 3] = 255;
        }
    }
    return image;
}

int ChildProbe(const std::filesystem::path& runtime, RuntimeCallerMode mode) {
    try {
        std::cout << "[SELFTEST] caller_mode=" << RuntimeCallerModeName(mode) << "\n";
        // Runtime compatibility diagnosis intentionally uses the V0.6.2 synchronization path
        // so a performance-test regression cannot be mistaken for an RTX40 runtime failure.
        _putenv_s("DLSS5_DISABLE_D3D12_BATCH", "1");
        D3D12Context d3d;
        DXGI_ADAPTER_DESC1 desc{};
        if (d3d.Adapter()) d3d.Adapter()->GetDesc1(&desc);
        std::cout << "[SELFTEST] GPU=" << Narrow(desc.Description) << "\n";
        DlssNrSettings settings{};
        settings.iterations = 1;
        DlssNrRunner runner(d3d, runtime, settings, mode);
        const auto input = MakeProbeImage();
        const auto output = runner.Process(input, nullptr);
        if (output.width != input.width || output.height != input.height || output.pixels.size() != input.pixels.size()) {
            throw std::runtime_error("Feature 18 returned an invalid output image");
        }
        size_t changed = 0;
        for (size_t i = 0; i < input.pixels.size(); ++i) if (input.pixels[i] != output.pixels[i]) ++changed;
        std::cout << "[SELFTEST] Feature18 Create/Evaluate PASS; changed_bytes=" << changed << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[SELFTEST] FAIL: " << e.what() << "\n";
        return 10;
    }
}

std::wstring Quote(const std::filesystem::path& p) { return L"\"" + p.wstring() + L"\""; }

DWORD RunChild(const std::filesystem::path& exe, const std::filesystem::path& runtime, RuntimeCallerMode mode) {
    std::wstring cmd = Quote(exe) + L" --child --runtime " + Quote(runtime) + L" --caller-mode " +
        (mode == RuntimeCallerMode::Direct ? L"direct" : L"legacy_hook");
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> writable(cmd.begin(), cmd.end()); writable.push_back(L'\0');
    if (!CreateProcessW(nullptr, writable.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) {
        std::cerr << "[SELFTEST] CreateProcess failed: " << GetLastError() << "\n";
        return 0xFFFFFFFFu;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return code;
}
}

int wmain(int argc, wchar_t** argv) {
    std::filesystem::path runtime = app::DefaultRuntimeDll();
    bool child = false;
    std::optional<RuntimeCallerMode> mode;
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--child") child = true;
        else if (a == L"--runtime" && i + 1 < argc) runtime = argv[++i];
        else if (a == L"--caller-mode" && i + 1 < argc) {
            const std::wstring v = argv[++i];
            if (v == L"direct") mode = RuntimeCallerMode::Direct;
            else if (v == L"legacy_hook") mode = RuntimeCallerMode::LegacyHook;
            else { std::cerr << "Unknown caller mode\n"; return 2; }
        }
    }
    if (!std::filesystem::exists(runtime)) {
        std::cerr << "Runtime missing: " << runtime.string() << "\n";
        return 3;
    }
    if (child) return ChildProbe(runtime, mode.value_or(RuntimeCallerMode::LegacyHook));

    std::cout << "Crow-DLSS5-Video-Image-Converter Runtime Self-Test V0.6.6-alpha2 (V0.6.2 stable compatibility path)\n";
    std::cout << "Runtime: " << runtime.string() << "\n";
    std::cout << "SHA256:  " << Sha256(runtime) << "\n";
    const auto profile = LoadRuntimeCompatProfile(runtime);
    std::cout << "Profile: " << profile.profile << "\n";
    std::cout << "Configured caller mode: " << RuntimeCallerModeName(profile.callerMode) << "\n\n";

    const auto exe = app::ExecutablePath();
    std::cout << "=== Isolated test A: direct ===\n";
    const DWORD direct = RunChild(exe, runtime, RuntimeCallerMode::Direct);
    std::cout << "direct exit=0x" << std::hex << direct << std::dec << "\n\n";
    std::cout << "=== Isolated test B: legacy_hook (V0.6.2 stable behavior) ===\n";
    const DWORD hook = RunChild(exe, runtime, RuntimeCallerMode::LegacyHook);
    std::cout << "legacy_hook exit=0x" << std::hex << hook << std::dec << "\n\n";

    if (direct == 0 || hook == 0) {
        std::cout << "RESULT: at least one caller mode passed Feature 18 Create + Evaluate.\n";
        if (hook == 0) std::cout << "Recommended caller_mode=legacy_hook (preserves stable V0.6.2 behavior).\n";
        else std::cout << "Recommended caller_mode=direct for this experimental runtime.\n";
        return 0;
    }
    std::cout << "RESULT: no caller mode passed. Keep V0.6.2 as the stable rollback baseline.\n";
    return 1;
}
