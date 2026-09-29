#pragma once

#include <cstdint>

namespace video {

enum class NvofQuality { Fast = 0, Medium = 1, Slow = 2 };

// Shared NR/FG guidance conditioning. V0.7.2-alpha6 adds soft visibility,
// occlusion/disocclusion topology and continuous uncertainty on top of the existing
// Cost/FB/photometric + spatial analysis. NR uses those signals for history rejection;
// FG uses them to gate local/depth repair and trailing multi-frame temporal consensus.
enum class NvofReliabilityMode { Off = 0, Auto = 1, Strong = 2 };

// User-facing NVOF D3D12 tuning. Defaults preserve the first alpha2 hardware-validated path.
struct NvofSettings {
    NvofQuality quality = NvofQuality::Slow;
    uint32_t outputGridSize = 4; // Supported values: 1, 2, 4. Device capability is validated at runtime.
    bool temporalHints = true;
    bool outputCost = true;
    NvofReliabilityMode reliability = NvofReliabilityMode::Auto;
};

} // namespace video
