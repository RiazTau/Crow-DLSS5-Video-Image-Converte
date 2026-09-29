#include "NvofFlowSession.h"
#include "NvofD3D12Bridge.h"
#include "NvofFlowPostprocess.h"
#include "NvofRuntimeProbe.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

#ifndef DLSS5_HAS_NVOF_SDK
#define DLSS5_HAS_NVOF_SDK 0
#endif
#ifndef DLSS5_NVOF_D3D12_BRIDGE_READY
#define DLSS5_NVOF_D3D12_BRIDGE_READY 0
#endif

namespace video {
namespace {

float SceneCutScore(const Rgba8Image& a, const Rgba8Image& b) {
    if (a.width != b.width || a.height != b.height || a.pixels.size() != b.pixels.size() || a.pixels.empty()) return 1.0f;
    constexpr size_t bins = 32;
    std::array<uint32_t, bins> ha{};
    std::array<uint32_t, bins> hb{};
    double mad = 0.0;
    uint64_t samples = 0;
    // Scene-cut detection is intentionally much cheaper than CPU optical flow. Sample a
    // sparse 4x4 lattice; NVOFA remains responsible for all actual motion estimation.
    for (uint32_t y = 0; y < a.height; y += 4u) {
        for (uint32_t x = 0; x < a.width; x += 4u) {
            const size_t i = (static_cast<size_t>(y) * a.width + x) * 4u;
            const auto luma = [](const uint8_t* p) {
                return 0.2126 * static_cast<double>(p[0]) + 0.7152 * static_cast<double>(p[1]) + 0.0722 * static_cast<double>(p[2]);
            };
            const double va = luma(&a.pixels[i]);
            const double vb = luma(&b.pixels[i]);
            ha[std::min<size_t>(bins - 1u, static_cast<size_t>(va * bins / 256.0))]++;
            hb[std::min<size_t>(bins - 1u, static_cast<size_t>(vb * bins / 256.0))]++;
            mad += std::abs(va - vb);
            ++samples;
        }
    }
    if (!samples) return 1.0f;
    double hist = 0.0;
    for (size_t i = 0; i < bins; ++i) hist += std::abs(static_cast<double>(ha[i]) - static_cast<double>(hb[i]));
    hist = 0.5 * hist / static_cast<double>(samples);
    const double madNorm = mad / static_cast<double>(samples) / 255.0;
    return static_cast<float>(std::clamp(0.65 * hist + 0.35 * madNorm, 0.0, 1.0));
}

TemporalFlowResult ResetResult(uint32_t width, uint32_t height, float score) {
    TemporalFlowResult r;
    r.sceneCut = true;
    r.sceneCutScore = score;
    const size_t n = static_cast<size_t>(width) * height;
    r.motionXY.assign(n * 2u, 0.0f);
    r.confidence.assign(n, 0.0f);
    return r;
}

} // namespace

bool NvofFlowSession::NativeBackendCompiled() noexcept {
    return DLSS5_HAS_NVOF_SDK != 0 && DLSS5_NVOF_D3D12_BRIDGE_READY != 0;
}

std::wstring NvofFlowSession::BuildStatusText() {
#if !DLSS5_HAS_NVOF_SDK
    return L"NVOF SDK headers were not supplied at configure time. Run NVOF_SDK_SETUP.bat, then rebuild.";
#elif !DLSS5_NVOF_D3D12_BRIDGE_READY
    return L"NVOF SDK headers were found, but the SDK 5.x D3D12 ABI compile probe failed. Check the selected Optical Flow SDK package and MSVC/Windows SDK, then rebuild.";
#else
    return L"Native NVOF D3D12 execute bridge compiled (SDK ABI validated at configure time).";
#endif
}

NvofFlowSession::NvofFlowSession(D3D12Context& d3d,
                                 uint32_t sourceWidth,
                                 uint32_t sourceHeight,
                                 float sceneCutThreshold,
                                 const NvofSettings& settings,
                                 std::atomic_bool* cancel)
    : _d3d(d3d), _sourceWidth(sourceWidth), _sourceHeight(sourceHeight),
      _sceneCutThreshold(sceneCutThreshold), _settings(settings), _cancel(cancel) {
    if (!_sourceWidth || !_sourceHeight) throw std::runtime_error("NVOF source dimensions are invalid");
    const auto runtime = ProbeNvofRuntime();
    if (!runtime.moduleLoaded || !runtime.d3d12EntryPoint) {
        throw std::runtime_error("NVIDIA Optical Flow D3D12 runtime is unavailable. Update the NVIDIA display driver and run Crow-DLSS5-Video-Image-Converter-NVOF-Self-Test.exe.");
    }
    if (!NativeBackendCompiled()) {
        const auto text = BuildStatusText();
        throw std::runtime_error(std::string(text.begin(), text.end()));
    }
    _bridge = std::make_unique<NvofD3D12Bridge>(_d3d, _sourceWidth, _sourceHeight, _settings);
}

NvofFlowSession::~NvofFlowSession() = default;

TemporalFlowResult NvofFlowSession::Process(const Rgba8Image& frame) {
    if (frame.width != _sourceWidth || frame.height != _sourceHeight ||
        frame.pixels.size() != static_cast<size_t>(_sourceWidth) * _sourceHeight * 4u) {
        throw std::runtime_error("NVOF optical-flow frame dimensions changed");
    }
    if (_cancel && _cancel->load(std::memory_order_relaxed)) throw std::runtime_error("NVOF processing cancelled");

    if (!_havePrevious) {
        _previous = frame;
        _havePrevious = true;
        _resetTemporalHints = true;
        return ResetResult(_sourceWidth, _sourceHeight, 1.0f);
    }

    const float score = SceneCutScore(_previous, frame);
    if (score >= _sceneCutThreshold) {
        _previous = frame;
        _resetTemporalHints = true;
        return ResetResult(_sourceWidth, _sourceHeight, score);
    }

    const bool disableTemporalHints = !_settings.temporalHints || _resetTemporalHints;
    auto native = _bridge->ExecuteCurrentToPrevious(frame, _previous, disableTemporalHints);
    NvofPostprocessInput pp{};
    pp.sourceWidth = _sourceWidth;
    pp.sourceHeight = _sourceHeight;
    pp.gridWidth = native.gridWidth;
    pp.gridHeight = native.gridHeight;
    pp.forward = &native.forward;
    pp.forwardCost = native.forwardCost.empty() ? nullptr : &native.forwardCost;
    pp.previousFrame = &_previous;
    pp.currentFrame = &frame;
    pp.sceneCutScore = score;
    pp.sceneCut = false;
    TemporalFlowResult result = PostprocessNvofFlow(pp);

    _previous = frame;
    _resetTemporalHints = false;
    return result;
}

} // namespace video
