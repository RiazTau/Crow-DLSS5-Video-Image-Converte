#pragma once

#include "D3D12Context.h"
#include "ImageWic.h"
#include "TemporalFlow.h"
#include "NvofConfig.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace video {

class NvofD3D12Bridge;

class NvofFlowSession {
public:
    NvofFlowSession(D3D12Context& d3d,
                    uint32_t sourceWidth,
                    uint32_t sourceHeight,
                    float sceneCutThreshold,
                    const NvofSettings& settings = {},
                    std::atomic_bool* cancel = nullptr);
    ~NvofFlowSession();
    NvofFlowSession(const NvofFlowSession&) = delete;
    NvofFlowSession& operator=(const NvofFlowSession&) = delete;

    TemporalFlowResult Process(const Rgba8Image& frame);

    static bool NativeBackendCompiled() noexcept;
    static std::wstring BuildStatusText();

private:
    D3D12Context& _d3d;
    uint32_t _sourceWidth = 0;
    uint32_t _sourceHeight = 0;
    float _sceneCutThreshold = 0.28f;
    NvofSettings _settings{};
    std::atomic_bool* _cancel = nullptr;
    std::unique_ptr<NvofD3D12Bridge> _bridge;
    Rgba8Image _previous;
    bool _havePrevious = false;
    bool _resetTemporalHints = true;
};

} // namespace video
