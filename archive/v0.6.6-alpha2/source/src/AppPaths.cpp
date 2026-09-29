#include "AppPaths.h"
#include <Windows.h>
#include <stdexcept>
#include <vector>

namespace app {
std::filesystem::path ExecutablePath() {
    std::vector<wchar_t> buffer(32768);
    DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!len || len >= buffer.size()) {
        throw std::runtime_error("GetModuleFileNameW failed");
    }
    return std::filesystem::path(buffer.data(), buffer.data() + len);
}
std::filesystem::path ExecutableDir() { return ExecutablePath().parent_path(); }
std::filesystem::path DefaultRuntimeDll() { return ExecutableDir() / L"runtime" / L"nvngx_dlssnr.dll"; }
std::filesystem::path DefaultDepthModel() { return ExecutableDir() / L"models" / L"depth_anything_v2" / L"model_fp16.onnx"; }
}
