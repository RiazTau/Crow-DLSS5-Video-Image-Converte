#pragma once
#include "D3D12Context.h"
#include "ImageWic.h"
#include "SnippetCallerCompat.h"
#include "RuntimeCompat.h"
#include <nvsdk_ngx.h>
#include <filesystem>
#include <cstdint>
#include <vector>
#include <memory>
#include <optional>

struct DlssNrSettings {
    int preset = 0;
    int style = 0;
    float intensity = 1.0f;
    float localToneStrength = 1.0f;
    float localStructureStrength = 1.0f;
    float skinStructureStrength = -1.0f;
    bool autoMask = false;
    bool uiCorrection = false;
    bool depthInverted = true;
    uint32_t iterations = 1;
};

class DlssNrRunner {
public:
    DlssNrRunner(D3D12Context& d3d, std::filesystem::path runtimeDll, DlssNrSettings settings,
                 std::optional<RuntimeCallerMode> callerModeOverride = std::nullopt);
    ~DlssNrRunner();
    DlssNrRunner(const DlssNrRunner&) = delete;
    DlssNrRunner& operator=(const DlssNrRunner&) = delete;

    Rgba8Image Process(const Rgba8Image& input, const std::vector<float>* depth = nullptr);
    // V0.5 temporal video entry point. motionXY is current->previous in pixel units, XY interleaved.
    // reset must be true for the first frame and scene cuts. Unlike Process(), this evaluates exactly once.
    Rgba8Image ProcessTemporal(const Rgba8Image& input,
                               const std::vector<float>* depth,
                               const std::vector<float>* motionXY,
                               bool reset,
                               float mvecScaleX = 1.0f,
                               float mvecScaleY = 1.0f);

private:
    struct Resources;
    void InitializeCore();
    void InitializeSnippet();
    void Shutdown() noexcept;
    void AllocateParameters();
    void CreateFeature(Resources& r);
    void EnsureResources(uint32_t width, uint32_t height);
    void ReleaseFeatureHandle() noexcept;
    bool SetCreateParameters(uint32_t width, uint32_t height, DWORD* sehCode) noexcept;
    bool SetEvaluateParameters(Resources& r, bool reset, float mvecScaleX, float mvecScaleY, DWORD* sehCode) noexcept;
    void Evaluate(Resources& r, bool reset, float mvecScaleX = 1.0f, float mvecScaleY = 1.0f);
    void EvaluateRecorded(Resources& r, bool reset, float mvecScaleX, float mvecScaleY);
    Rgba8Image ProcessInternal(const Rgba8Image& input, const std::vector<float>* depth,
                               const std::vector<float>* motionXY, bool reset,
                               float mvecScaleX, float mvecScaleY, uint32_t evaluations);
    Rgba8Image ProcessInternalLegacy(const Rgba8Image& input, const std::vector<float>* depth,
                                     const std::vector<float>* motionXY, bool reset,
                                     float mvecScaleX, float mvecScaleY, uint32_t evaluations);
    Rgba8Image ProcessInternalBatched(const Rgba8Image& input, const std::vector<float>* depth,
                                      const std::vector<float>* motionXY, bool reset,
                                      float mvecScaleX, float mvecScaleY, uint32_t evaluations);

    D3D12Context& _d3d;
    std::filesystem::path _runtimeDll;
    std::filesystem::path _runtimeDir;
    DlssNrSettings _settings;
    RuntimeCompatProfile _runtimeCompat;

    NVSDK_NGX_Parameter* _parameters = nullptr;
    NVSDK_NGX_Handle* _feature = nullptr;
    HMODULE _snippetModule = nullptr;
    SnippetCallerCompat _callerCompat;
    bool _coreInitialized = false;
    bool _snippetInitialized = false;
    bool _performanceBatching = true;
    std::unique_ptr<Resources> _resources;

    using SnippetInitExtFn = NVSDK_NGX_Result(NVSDK_CONV*)(
        unsigned long long, const wchar_t*, ID3D12Device*, NVSDK_NGX_Version,
        const NVSDK_NGX_Parameter*);
    using CreateFeatureFn = NVSDK_NGX_Result(NVSDK_CONV*)(
        ID3D12GraphicsCommandList*, NVSDK_NGX_Feature, NVSDK_NGX_Parameter*,
        NVSDK_NGX_Handle**);
    using EvaluateFeatureFn = NVSDK_NGX_Result(NVSDK_CONV*)(
        ID3D12GraphicsCommandList*, const NVSDK_NGX_Handle*,
        const NVSDK_NGX_Parameter*, PFN_NVSDK_NGX_ProgressCallback);
    using ReleaseFeatureFn = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Handle*);
    using ShutdownFn = NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12Device*);

    SnippetInitExtFn _snippetInit = nullptr;
    CreateFeatureFn _createFeature = nullptr;
    EvaluateFeatureFn _evaluateFeature = nullptr;
    ReleaseFeatureFn _releaseFeature = nullptr;
    ShutdownFn _snippetShutdown = nullptr;
};
