#include "NvofRuntimeProbe.h"
#include <Windows.h>

namespace video {

NvofRuntimeStatus ProbeNvofRuntime() noexcept {
    NvofRuntimeStatus status;
    HMODULE module = LoadLibraryExW(L"nvofapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) module = LoadLibraryW(L"nvofapi64.dll");
    if (!module) {
        status.message = L"nvofapi64.dll was not found. Install/update the NVIDIA display driver.";
        return status;
    }

    status.moduleLoaded = true;
    status.moduleName = L"nvofapi64.dll";
    status.d3d12EntryPoint = GetProcAddress(module, "NvOFAPICreateInstanceD3D12") != nullptr;
    status.maxVersionEntryPoint = GetProcAddress(module, "NvOFGetMaxSupportedApiVersion") != nullptr;

    if (!status.d3d12EntryPoint) {
        status.message = L"NVIDIA Optical Flow runtime loaded, but the DirectX 12 entry point is unavailable.";
    } else if (!status.maxVersionEntryPoint) {
        status.message = L"NVIDIA Optical Flow DirectX 12 entry point is present; max-version query export was not found.";
    } else {
        status.message = L"NVIDIA Optical Flow driver runtime and DirectX 12 entry point are present.";
    }
    FreeLibrary(module);
    return status;
}

} // namespace video
