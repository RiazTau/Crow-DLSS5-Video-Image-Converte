#pragma once

#include "D3D12Context.h"
#include "ImageWic.h"
#include <nvsdk_ngx.h>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fg {

struct DlssFgCapability {
    bool available = false;
    int featureInitResult = 0;
    uint32_t maxGeneratedFrames = 0;
    uint32_t maxMultiplier = 2;
    bool isRtx40Series = false;
    bool isRtx50Series = false;
    std::wstring adapterName;
    std::wstring message;
};

// Offline DLSS Frame Generation / Multi Frame Generation backend.
// Contract used by V0.7.2-alpha3:
//   color  : RGBA8, full resolution
//   motion : current -> previous, full-resolution source pixels
//   depth  : optional R32_FLOAT full-resolution guidance; zero fallback when omitted
class DlssFrameGenerationRunner {
public:
    DlssFrameGenerationRunner(D3D12Context& d3d, std::filesystem::path runtimeDirectory, bool manageCoreLifetime = true);
    ~DlssFrameGenerationRunner();
    DlssFrameGenerationRunner(const DlssFrameGenerationRunner&) = delete;
    DlssFrameGenerationRunner& operator=(const DlssFrameGenerationRunner&) = delete;

    const DlssFgCapability& Capability() const noexcept { return _capability; }

    // Evaluate the current rendered/source frame. The feature keeps temporal history internally.
    // Returns an interpolated frame between the previous real frame and current real frame when
    // interpolation is valid. On reset / SDK-disable it returns std::nullopt.
    std::optional<Rgba8Image> GenerateFrame(const Rgba8Image& current,
                                             const std::vector<float>& currentToPreviousMotionXY,
                                             const std::vector<float>* depth,
                                             bool depthInverted,
                                             bool reset,
                                             uint64_t backbufferFrameId,
                                             uint32_t multiFrameCount,
                                             uint32_t multiFrameIndex);

    void RequestReset() noexcept { _forceReset = true; }
    void Drain();

private:
    struct Resources;

    void InitializeCore();
    void QueryCapability();
    void EnsureFeature(uint32_t width, uint32_t height);
    void CreateFeature(uint32_t width, uint32_t height);
    void ReleaseFeature() noexcept;
    void Shutdown() noexcept;

    D3D12Context& _d3d;
    std::filesystem::path _runtimeDirectory;
    std::filesystem::path _runtimeDll;
    NVSDK_NGX_Parameter* _parameters = nullptr;
    NVSDK_NGX_Handle* _feature = nullptr;
    bool _coreInitialized = false;
    bool _manageCoreLifetime = true;
    bool _forceReset = true;
    DlssFgCapability _capability{};
    std::unique_ptr<Resources> _resources;
};

} // namespace fg
