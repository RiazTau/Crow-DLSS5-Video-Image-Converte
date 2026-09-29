#include "NgxCoreSession.h"
#include "AppPaths.h"
#include <nvsdk_ngx.h>
#include <Windows.h>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
constexpr const char* kProjectId = "1b4fbf82-8544-4a4c-bb14-d59f1b86b73a";
constexpr const char* kEngineVersion = "Crow-DLSS-Rendering-Tool-0.7.2-alpha2";

LONG CaptureNgxException(DWORD code, DWORD* sehCode) noexcept {
    *sehCode = code;
    return EXCEPTION_EXECUTE_HANDLER;
}

NVSDK_NGX_Result SafeInit(const wchar_t* appData, ID3D12Device* device,
                          const NVSDK_NGX_FeatureCommonInfo* info, DWORD* seh) noexcept {
    *seh = 0;
    __try {
        return NVSDK_NGX_D3D12_Init_with_ProjectID(
            kProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, kEngineVersion,
            appData, device, info, NVSDK_NGX_Version_API);
    } __except (CaptureNgxException(GetExceptionCode(), seh)) {
        return NVSDK_NGX_Result_FAIL_PlatformError;
    }
}

NVSDK_NGX_Result SafeShutdown(ID3D12Device* device, DWORD* seh) noexcept {
    *seh = 0;
    __try { return NVSDK_NGX_D3D12_Shutdown1(device); }
    __except (CaptureNgxException(GetExceptionCode(), seh)) { return NVSDK_NGX_Result_FAIL_PlatformError; }
}

void CheckNgx(NVSDK_NGX_Result result, const char* what) {
    if (!NVSDK_NGX_SUCCEED(result)) {
        char b[192]{};
        sprintf_s(b, "%s failed: NGX result=0x%08X", what, static_cast<unsigned>(result));
        throw std::runtime_error(b);
    }
}
} // namespace

NgxCoreSession::NgxCoreSession(D3D12Context& d3d, std::vector<std::filesystem::path> searchPaths)
    : _d3d(d3d) {
    std::vector<std::wstring> storage;
    storage.reserve(searchPaths.size() + 1);
    storage.push_back(app::ExecutableDir().wstring());
    for (const auto& p : searchPaths) {
        if (!p.empty()) storage.push_back(std::filesystem::absolute(p).lexically_normal().wstring());
    }
    std::vector<const wchar_t*> paths;
    paths.reserve(storage.size());
    for (const auto& s : storage) paths.push_back(s.c_str());

    NVSDK_NGX_FeatureCommonInfo info{};
    info.PathListInfo.Path = paths.data();
    info.PathListInfo.Length = static_cast<unsigned int>(paths.size());
    DWORD seh = 0;
    const auto result = SafeInit(storage.front().c_str(), _d3d.Device(), &info, &seh);
    if (seh) {
        char b[160]{};
        sprintf_s(b, "Shared NGX Core Init raised SEH 0x%08X", static_cast<unsigned>(seh));
        throw std::runtime_error(b);
    }
    CheckNgx(result, "Shared NVSDK_NGX_D3D12_Init_with_ProjectID");
    _initialized = true;
    std::cout << "[NGX] Shared core initialized for NR + FG\n";
}

NgxCoreSession::~NgxCoreSession() {
    if (!_initialized) return;
    DWORD seh = 0;
    const auto result = SafeShutdown(_d3d.Device(), &seh);
    if (seh || !NVSDK_NGX_SUCCEED(result)) {
        std::cerr << "[NGX] Shared core shutdown warning: result=0x" << std::hex
                  << static_cast<unsigned>(result) << " seh=0x" << seh << std::dec << "\n";
    }
    _initialized = false;
}
