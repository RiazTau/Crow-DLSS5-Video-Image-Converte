#pragma once

#include <cstdint>

namespace video {

enum class NvofQuality { Fast = 0, Medium = 1, Slow = 2 };

// User-facing NVOF D3D12 tuning. Defaults preserve the first alpha2 hardware-validated path.
struct NvofSettings {
    NvofQuality quality = NvofQuality::Slow;
    uint32_t outputGridSize = 4; // Supported values: 1, 2, 4. Device capability is validated at runtime.
    bool temporalHints = true;
    bool outputCost = true;
};

} // namespace video
