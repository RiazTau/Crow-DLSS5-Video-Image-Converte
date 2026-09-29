#pragma once

#include <string>

namespace video {

struct NvofRuntimeStatus {
    bool moduleLoaded = false;
    bool d3d12EntryPoint = false;
    bool maxVersionEntryPoint = false;
    std::wstring moduleName;
    std::wstring message;
};

// Lightweight probe that deliberately does not require NVIDIA Optical Flow SDK headers.
// nvofapi64.dll is installed by the NVIDIA display driver; the SDK package is a separate
// build-time dependency and is never redistributed by this project.
NvofRuntimeStatus ProbeNvofRuntime() noexcept;

} // namespace video
