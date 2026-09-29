#pragma once

#include "D3D12Context.h"
#include "ImageWic.h"
#include "NvofFlowPostprocess.h"
#include "NvofConfig.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace video {

struct NvofNativeFrameResult {
    uint32_t gridWidth = 0;
    uint32_t gridHeight = 0;
    std::vector<NvofPackedVector> forward;
    std::vector<uint8_t> forwardCost;
};

// Thin ownership wrapper around the NVIDIA Optical Flow SDK 5.x D3D12 API.
// The public header deliberately contains no NVIDIA SDK types so source builds can still
// configure without redistributing the vendor headers. The real implementation is compiled
// only when CMake validates the SDK 5.x D3D12 ABI on MSVC.
class NvofD3D12Bridge {
public:
    NvofD3D12Bridge(D3D12Context& d3d, uint32_t width, uint32_t height, const NvofSettings& settings);
    ~NvofD3D12Bridge();
    NvofD3D12Bridge(const NvofD3D12Bridge&) = delete;
    NvofD3D12Bridge& operator=(const NvofD3D12Bridge&) = delete;

    NvofNativeFrameResult ExecuteCurrentToPrevious(const Rgba8Image& current,
                                                   const Rgba8Image& previous,
                                                   bool disableTemporalHints);

    uint32_t GridSize() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace video
