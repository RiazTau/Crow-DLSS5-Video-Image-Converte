#include "DlssNrRunner.h"
#include "AppPaths.h"
#include "video/ParallelRows.h"
#include <nvsdk_ngx.h>
#include <Windows.h>
#include <wrl/client.h>
#include <array>
#include <bit>
#include <limits>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>
#include <cstring>
#include <cstdlib>

using Microsoft::WRL::ComPtr;

namespace {
constexpr NVSDK_NGX_Feature kFeatureDlssNr = static_cast<NVSDK_NGX_Feature>(18);
constexpr unsigned long long kSignedSnippetAppId = 0x0876232Cull;
constexpr const char* kProjectId = "1b4fbf82-8544-4a4c-bb14-d59f1b86b73a";
constexpr const char* kEngineVersion = "Crow-DLSS5-Video-Image-Converter-0.5";

constexpr char P_WIDTH[] = "DLSSNR.Width";
constexpr char P_HEIGHT[] = "DLSSNR.Height";
constexpr char P_INPUT_WIDTH[] = "DLSSNR.InputWidth";
constexpr char P_INPUT_HEIGHT[] = "DLSSNR.InputHeight";
constexpr char P_OUTPUT_WIDTH[] = "DLSSNR.OutputWidth";
constexpr char P_OUTPUT_HEIGHT[] = "DLSSNR.OutputHeight";
constexpr char P_OUTPUT_DOT_WIDTH[] = "DLSSNR.Output.Width";
constexpr char P_OUTPUT_DOT_HEIGHT[] = "DLSSNR.Output.Height";
constexpr char P_UPSCALING[] = "DLSSNR.Upscaling";
constexpr char P_SCALE[] = "DLSSNR.Scale";
constexpr char P_SCALING_RATIO[] = "DLSSNR.ScalingRatio";
constexpr char P_SCALING_RATIO_CALLBACK[] = "DLSSNRComputeScalingRatioCallback";
constexpr char P_PRESET[] = "DLSSNR.Hint.Render.Preset";
constexpr char P_COLOR[] = "DLSSNR.Color";
constexpr char P_OUTPUT[] = "DLSSNR.Output";
constexpr char P_MVEC[] = "DLSSNR.MVec";
constexpr char P_DEPTH[] = "DLSSNR.Depth";
constexpr char P_MVEC_SCALE_X[] = "DLSSNR.MVecScaleX";
constexpr char P_MVEC_SCALE_Y[] = "DLSSNR.MVecScaleY";
constexpr char P_DEPTH_INVERTED[] = "DLSSNR.DepthInverted";
constexpr char P_ENABLED[] = "DLSSNR.Enabled";
constexpr char P_RESET[] = "DLSSNR.Reset";
constexpr char P_STYLE[] = "DLSSNR.Style";
constexpr char P_INTENSITY[] = "DLSSNR.Intensity";
constexpr char P_LOCAL_TONE[] = "DLSSNR.LocalToneStrength";
constexpr char P_LOCAL_STRUCTURE[] = "DLSSNR.LocalStructureStrength";
constexpr char P_SKIN_STRUCTURE[] = "DLSSNR.SkinStructureStrength";
constexpr char P_AUTO_MASK[] = "DLSSNR.UseAutoMask";
constexpr char P_UI_CORRECTION[] = "DLSSNR.UICorrection";
constexpr char P_INDICATOR_INVERT_X[] = "DLSS.Indicator.Invert.X.Axis";
constexpr char P_INDICATOR_INVERT_Y[] = "DLSS.Indicator.Invert.Y.Axis";

struct SubrectNames { const char* x; const char* y; const char* w; const char* h; };
constexpr SubrectNames kSubrects[] = {
    {"DLSSNR.ColorSubrectBaseX", "DLSSNR.ColorSubrectBaseY", "DLSSNR.ColorSubrectWidth", "DLSSNR.ColorSubrectHeight"},
    {"DLSSNR.OutputSubrectBaseX", "DLSSNR.OutputSubrectBaseY", "DLSSNR.OutputSubrectWidth", "DLSSNR.OutputSubrectHeight"},
    {"DLSSNR.MVecSubrectBaseX", "DLSSNR.MVecSubrectBaseY", "DLSSNR.MVecSubrectWidth", "DLSSNR.MVecSubrectHeight"},
    {"DLSSNR.DepthSubrectBaseX", "DLSSNR.DepthSubrectBaseY", "DLSSNR.DepthSubrectWidth", "DLSSNR.DepthSubrectHeight"}
};

void CheckNgx(NVSDK_NGX_Result result, const char* what) {
    if (!NVSDK_NGX_SUCCEED(result)) {
        char buf[160]{};
        sprintf_s(buf, "%s failed: NGX result=0x%08X", what, static_cast<unsigned>(result));
        throw std::runtime_error(buf);
    }
}

NVSDK_NGX_Result NVSDK_CONV ScalingRatioCallback(NVSDK_NGX_Parameter* p) noexcept {
    __try {
        if (!p) return NVSDK_NGX_Result_FAIL_InvalidParameter;
        p->Set(P_SCALING_RATIO, 1.0f);
        return NVSDK_NGX_Result_Success;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return NVSDK_NGX_Result_FAIL_PlatformError;
    }
}


uint16_t FloatToHalf(float value) noexcept {
    const uint32_t bits = std::bit_cast<uint32_t>(value);
    const uint32_t sign = (bits >> 16u) & 0x8000u;
    const uint32_t mantissa = bits & 0x007FFFFFu;
    const int32_t exponent = static_cast<int32_t>((bits >> 23u) & 0xFFu) - 127;
    if (exponent == 128) {
        if (mantissa) return static_cast<uint16_t>(sign | 0x7E00u);
        return static_cast<uint16_t>(sign | 0x7C00u);
    }
    if (exponent > 15) return static_cast<uint16_t>(sign | 0x7C00u);
    if (exponent < -24) return static_cast<uint16_t>(sign);
    if (exponent < -14) {
        const uint32_t m = mantissa | 0x00800000u;
        const int shift = (-14 - exponent) + 13;
        uint32_t halfMantissa = m >> shift;
        if (shift > 0 && ((m >> (shift - 1)) & 1u)) halfMantissa += 1u;
        return static_cast<uint16_t>(sign | (halfMantissa & 0x03FFu));
    }
    uint32_t halfExponent = static_cast<uint32_t>(exponent + 15) << 10u;
    uint32_t halfMantissa = mantissa >> 13u;
    if (mantissa & 0x00001000u) {
        halfMantissa += 1u;
        if (halfMantissa & 0x00000400u) {
            halfMantissa = 0;
            halfExponent += 0x0400u;
            if (halfExponent >= 0x7C00u) halfExponent = 0x7C00u;
        }
    }
    return static_cast<uint16_t>(sign | halfExponent | (halfMantissa & 0x03FFu));
}

template<typename Fn>
void* FunctionAddress(Fn fn) {
    void* p = nullptr;
    static_assert(sizeof(p) == sizeof(fn));
    std::memcpy(&p, &fn, sizeof(p));
    return p;
}

void SetSubrect(NVSDK_NGX_Parameter* p, const SubrectNames& n, uint32_t width, uint32_t height) {
    p->Set(n.x, 0u); p->Set(n.y, 0u); p->Set(n.w, width); p->Set(n.h, height);
}

template<typename T>
T GetExport(HMODULE module, const char* name) {
    auto p = GetProcAddress(module, name);
    if (!p) throw std::runtime_error(std::string("Missing export in nvngx_dlssnr.dll: ") + name);
    return reinterpret_cast<T>(p);
}

using SafeSnippetInitExtFn = NVSDK_NGX_Result(NVSDK_CONV*)(
    unsigned long long, const wchar_t*, ID3D12Device*, NVSDK_NGX_Version,
    const NVSDK_NGX_Parameter*);
using SafeCreateFeatureFn = NVSDK_NGX_Result(NVSDK_CONV*)(
    ID3D12GraphicsCommandList*, NVSDK_NGX_Feature, NVSDK_NGX_Parameter*, NVSDK_NGX_Handle**);
using SafeEvaluateFeatureFn = NVSDK_NGX_Result(NVSDK_CONV*)(
    ID3D12GraphicsCommandList*, const NVSDK_NGX_Handle*, const NVSDK_NGX_Parameter*,
    PFN_NVSDK_NGX_ProgressCallback);
using SafeReleaseFeatureFn = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Handle*);
using SafeShutdownFn = NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12Device*);

LONG CaptureNgxException(DWORD code, DWORD* sehCode) noexcept {
    *sehCode = code;
    return EXCEPTION_EXECUTE_HANDLER;
}

NVSDK_NGX_Result SafeCoreInit(
    const wchar_t* applicationDataPath, ID3D12Device* device,
    const NVSDK_NGX_FeatureCommonInfo* featureInfo, DWORD* sehCode) noexcept {
    *sehCode = 0;
    __try {
        return NVSDK_NGX_D3D12_Init_with_ProjectID(
            kProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, kEngineVersion,
            applicationDataPath, device, featureInfo, NVSDK_NGX_Version_API);
    } __except (CaptureNgxException(GetExceptionCode(), sehCode)) {
        return NVSDK_NGX_Result_FAIL_PlatformError;
    }
}

NVSDK_NGX_Result SafeAllocateParameters(NVSDK_NGX_Parameter** parameters, DWORD* sehCode) noexcept {
    *sehCode = 0;
    __try {
        return NVSDK_NGX_D3D12_AllocateParameters(parameters);
    } __except (CaptureNgxException(GetExceptionCode(), sehCode)) {
        return NVSDK_NGX_Result_FAIL_PlatformError;
    }
}

NVSDK_NGX_Result SafeDestroyParameters(NVSDK_NGX_Parameter* parameters, DWORD* sehCode) noexcept {
    *sehCode = 0;
    __try {
        return NVSDK_NGX_D3D12_DestroyParameters(parameters);
    } __except (CaptureNgxException(GetExceptionCode(), sehCode)) {
        return NVSDK_NGX_Result_FAIL_PlatformError;
    }
}

NVSDK_NGX_Result SafeCoreShutdown(ID3D12Device* device, DWORD* sehCode) noexcept {
    *sehCode = 0;
    __try {
        return NVSDK_NGX_D3D12_Shutdown1(device);
    } __except (CaptureNgxException(GetExceptionCode(), sehCode)) {
        return NVSDK_NGX_Result_FAIL_PlatformError;
    }
}

NVSDK_NGX_Result SafeSnippetInit(
    SafeSnippetInitExtFn fn, unsigned long long appId, const wchar_t* dataPath,
    ID3D12Device* device, DWORD* sehCode) noexcept {
    *sehCode = 0;
    __try {
        return fn(appId, dataPath, device, NVSDK_NGX_Version_API, nullptr);
    } __except (CaptureNgxException(GetExceptionCode(), sehCode)) {
        return NVSDK_NGX_Result_FAIL_PlatformError;
    }
}

NVSDK_NGX_Result SafeCreateFeature(
    SafeCreateFeatureFn fn, ID3D12GraphicsCommandList* list, NVSDK_NGX_Feature feature,
    NVSDK_NGX_Parameter* parameters, NVSDK_NGX_Handle** handle, DWORD* sehCode) noexcept {
    *sehCode = 0;
    __try {
        return fn(list, feature, parameters, handle);
    } __except (CaptureNgxException(GetExceptionCode(), sehCode)) {
        return NVSDK_NGX_Result_FAIL_PlatformError;
    }
}

NVSDK_NGX_Result SafeEvaluateFeature(
    SafeEvaluateFeatureFn fn, ID3D12GraphicsCommandList* list, const NVSDK_NGX_Handle* handle,
    const NVSDK_NGX_Parameter* parameters, DWORD* sehCode) noexcept {
    *sehCode = 0;
    __try {
        return fn(list, handle, parameters, nullptr);
    } __except (CaptureNgxException(GetExceptionCode(), sehCode)) {
        return NVSDK_NGX_Result_FAIL_PlatformError;
    }
}

NVSDK_NGX_Result SafeReleaseFeature(
    SafeReleaseFeatureFn fn, NVSDK_NGX_Handle* handle, DWORD* sehCode) noexcept {
    *sehCode = 0;
    __try {
        return fn(handle);
    } __except (CaptureNgxException(GetExceptionCode(), sehCode)) {
        return NVSDK_NGX_Result_FAIL_PlatformError;
    }
}

NVSDK_NGX_Result SafeShutdown(
    SafeShutdownFn fn, ID3D12Device* device, DWORD* sehCode) noexcept {
    *sehCode = 0;
    __try {
        return fn(device);
    } __except (CaptureNgxException(GetExceptionCode(), sehCode)) {
        return NVSDK_NGX_Result_FAIL_PlatformError;
    }
}

bool PerformanceBatchingEnabled() noexcept {
    const char* disabled = std::getenv("DLSS5_DISABLE_D3D12_BATCH");
    if (!disabled || !*disabled) return true;
    return std::strcmp(disabled, "0") == 0 || _stricmp(disabled, "false") == 0 || _stricmp(disabled, "off") == 0;
}
}

struct DlssNrRunner::Resources {
    uint32_t width = 0;
    uint32_t height = 0;
    ComPtr<ID3D12Resource> color;
    ComPtr<ID3D12Resource> output;
    ComPtr<ID3D12Resource> motion;
    ComPtr<ID3D12Resource> depth;
    D3D12Context::TextureTransferBuffer colorUpload;
    D3D12Context::TextureTransferBuffer depthUpload;
    D3D12Context::TextureTransferBuffer motionUpload;
    D3D12Context::TextureTransferBuffer outputReadback;
    std::vector<float> zeroDepth;
    std::vector<uint16_t> zeroMotion;
    std::vector<uint16_t> packedMotion;
    bool depthIsZero = false;
    bool motionIsZero = false;
};

DlssNrRunner::DlssNrRunner(D3D12Context& d3d, std::filesystem::path runtimeDll, DlssNrSettings settings,
                             std::optional<RuntimeCallerMode> callerModeOverride)
    : _d3d(d3d), _runtimeDll(std::move(runtimeDll)), _runtimeDir(_runtimeDll.parent_path()), _settings(settings),
      _runtimeCompat(LoadRuntimeCompatProfile(_runtimeDll, callerModeOverride)) {
    if (_settings.iterations == 0) _settings.iterations = 1;
    _performanceBatching = PerformanceBatchingEnabled();
    std::cout << "[PERF] D3D12 frame batching: " << (_performanceBatching ? "ON" : "OFF (legacy sync path)") << "\n";
    if (!std::filesystem::exists(_runtimeDll)) {
        throw std::runtime_error("nvngx_dlssnr.dll not found: " + _runtimeDll.string());
    }
    InitializeCore();
    InitializeSnippet();
    AllocateParameters();
}

DlssNrRunner::~DlssNrRunner() { Shutdown(); }

void DlssNrRunner::InitializeCore() {
    const auto appData = app::ExecutableDir().wstring();
    const std::wstring runtime = _runtimeDir.wstring();
    const wchar_t* paths[] = { appData.c_str(), runtime.c_str() };
    NVSDK_NGX_FeatureCommonInfo info{};
    info.PathListInfo.Path = paths;
    info.PathListInfo.Length = 2;
    DWORD sehCode = 0;
    const auto result = SafeCoreInit(appData.c_str(), _d3d.Device(), &info, &sehCode);
    if (sehCode) {
        char buf[128]{};
        sprintf_s(buf, "NGX Core Init raised SEH 0x%08X", static_cast<unsigned>(sehCode));
        throw std::runtime_error(buf);
    }
    CheckNgx(result, "NVSDK_NGX_D3D12_Init_with_ProjectID");
    _coreInitialized = true;
    std::cout << "[NGX] Core initialized\n";
}

void DlssNrRunner::InitializeSnippet() {
    _snippetModule = LoadLibraryExW(_runtimeDll.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!_snippetModule) throw std::runtime_error("LoadLibraryExW(nvngx_dlssnr.dll) failed");

    _snippetInit = GetExport<SnippetInitExtFn>(_snippetModule, "NVSDK_NGX_D3D12_Init_Ext");
    _createFeature = GetExport<CreateFeatureFn>(_snippetModule, "NVSDK_NGX_D3D12_CreateFeature");
    _evaluateFeature = GetExport<EvaluateFeatureFn>(_snippetModule, "NVSDK_NGX_D3D12_EvaluateFeature");
    _releaseFeature = GetExport<ReleaseFeatureFn>(_snippetModule, "NVSDK_NGX_D3D12_ReleaseFeature");
    _snippetShutdown = GetExport<ShutdownFn>(_snippetModule, "NVSDK_NGX_D3D12_Shutdown1");

    // V0.6.2 (40-Series Compatibility) keeps the V0.6.1.2 RTX50 path byte-for-byte
    // in behavior unless an explicit compatibility profile requests another caller mode.
    if (_runtimeCompat.callerMode == RuntimeCallerMode::LegacyHook) {
        if (!_callerCompat.Install(_snippetModule)) {
            throw std::runtime_error("Failed to install DLSSNR signed-snippet caller compatibility hook");
        }
        std::cout << "[DLSSNR] Caller mode: legacy_hook"
                  << (_runtimeCompat.experimental ? " (experimental profile)" : "") << "\n";
    } else {
        std::cout << "[DLSSNR] Caller mode: direct (RTX40 compatibility experiment)\n";
    }

    DWORD sehCode = 0;
    const auto appDir = app::ExecutableDir();
    const auto result = SafeSnippetInit(
        _snippetInit, kSignedSnippetAppId, appDir.c_str(), _d3d.Device(), &sehCode);
    if (sehCode) {
        char buf[128]{};
        sprintf_s(buf, "nvngx_dlssnr Init_Ext raised SEH 0x%08X", static_cast<unsigned>(sehCode));
        throw std::runtime_error(buf);
    }
    CheckNgx(result, "nvngx_dlssnr Init_Ext");
    _snippetInitialized = true;
    std::cout << "[DLSSNR] Signed runtime initialized\n";
}

void DlssNrRunner::AllocateParameters() {
    DWORD sehCode = 0;
    const auto result = SafeAllocateParameters(&_parameters, &sehCode);
    if (sehCode) {
        char buf[128]{};
        sprintf_s(buf, "NGX AllocateParameters raised SEH 0x%08X", static_cast<unsigned>(sehCode));
        throw std::runtime_error(buf);
    }
    CheckNgx(result, "NVSDK_NGX_D3D12_AllocateParameters");
    if (!_parameters) throw std::runtime_error("NGX returned null parameter block");
}

bool DlssNrRunner::SetCreateParameters(uint32_t width, uint32_t height, DWORD* sehCode) noexcept {
    *sehCode = 0;
    __try {
        _parameters->Set(P_WIDTH, width);
        _parameters->Set(P_HEIGHT, height);
        _parameters->Set(P_INPUT_WIDTH, width);
        _parameters->Set(P_INPUT_HEIGHT, height);
        _parameters->Set(P_OUTPUT_WIDTH, width);
        _parameters->Set(P_OUTPUT_HEIGHT, height);
        _parameters->Set(P_OUTPUT_DOT_WIDTH, width);
        _parameters->Set(P_OUTPUT_DOT_HEIGHT, height);
        _parameters->Set(P_UPSCALING, 0u);
        _parameters->Set(P_SCALE, 1.0f);
        _parameters->Set(P_SCALING_RATIO, 1.0f);
        _parameters->Set(P_SCALING_RATIO_CALLBACK, FunctionAddress(&ScalingRatioCallback));
        _parameters->Set(P_PRESET, _settings.preset);
        _parameters->Set(NVSDK_NGX_Parameter_Width, width);
        _parameters->Set(NVSDK_NGX_Parameter_Height, height);
        _parameters->Set(NVSDK_NGX_Parameter_PerfQualityValue,
            static_cast<int>(NVSDK_NGX_PerfQuality_Value_Balanced));
        _parameters->Set(NVSDK_NGX_Parameter_CreationNodeMask, 1u);
        _parameters->Set(NVSDK_NGX_Parameter_VisibilityNodeMask, 1u);
        return true;
    } __except (CaptureNgxException(GetExceptionCode(), sehCode)) {
        return false;
    }
}

void DlssNrRunner::CreateFeature(Resources& r) {
    DWORD sehCode = 0;
    if (!SetCreateParameters(r.width, r.height, &sehCode)) {
        char buf[128]{};
        sprintf_s(buf, "DLSSNR creation parameter setup raised SEH 0x%08X", static_cast<unsigned>(sehCode));
        throw std::runtime_error(buf);
    }
    _d3d.ResetList();
    sehCode = 0;
    const auto result = SafeCreateFeature(
        _createFeature, _d3d.List(), kFeatureDlssNr, _parameters, &_feature, &sehCode);
    if (sehCode) {
        char buf[128]{};
        sprintf_s(buf, "DLSSNR CreateFeature raised SEH 0x%08X", static_cast<unsigned>(sehCode));
        throw std::runtime_error(buf);
    }
    CheckNgx(result, "DLSSNR Feature 18 CreateFeature");
    if (!_feature) throw std::runtime_error("DLSSNR Feature 18 returned null handle");
    _d3d.ExecuteAndWait();
    std::cout << "[DLSSNR] Feature 18 created\n";
}


void DlssNrRunner::ReleaseFeatureHandle() noexcept {
    if (_feature && _releaseFeature) {
        DWORD sehCode = 0;
        const auto result = SafeReleaseFeature(_releaseFeature, _feature, &sehCode);
        if (sehCode) {
            std::cerr << "[DLSSNR] ReleaseFeature SEH=0x" << std::hex << sehCode << std::dec << "\n";
        } else if (!NVSDK_NGX_SUCCEED(result)) {
            std::cerr << "[DLSSNR] ReleaseFeature failed: 0x" << std::hex
                      << static_cast<unsigned>(result) << std::dec << "\n";
        }
        _feature = nullptr;
    }
}

void DlssNrRunner::EnsureResources(uint32_t width, uint32_t height) {
    if (_resources && _resources->width == width && _resources->height == height && _feature) return;

    ReleaseFeatureHandle();
    _resources.reset();

    auto r = std::make_unique<Resources>();
    r->width = width;
    r->height = height;
    r->color = _d3d.CreateTexture2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    r->output = _d3d.CreateTexture2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    r->motion = _d3d.CreateTexture2D(DXGI_FORMAT_R16G16_FLOAT, width, height,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    r->depth = _d3d.CreateTexture2D(DXGI_FORMAT_R32_FLOAT, width, height,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);

    // Persistent staging allocations remove four committed-resource allocations from every frame.
    // The legacy fallback does not pay the extra VRAM/system-memory footprint.
    if (_performanceBatching) {
        r->colorUpload = _d3d.CreateUploadTransferBuffer(r->color.Get());
        r->depthUpload = _d3d.CreateUploadTransferBuffer(r->depth.Get());
        r->motionUpload = _d3d.CreateUploadTransferBuffer(r->motion.Get());
        r->outputReadback = _d3d.CreateReadbackTransferBuffer(r->output.Get());
        r->packedMotion.resize(static_cast<size_t>(width) * height * 2u);
    }

    r->zeroMotion.assign(static_cast<size_t>(width) * height * 2u, 0u);
    _d3d.UploadTexture2D(r->motion.Get(), r->zeroMotion.data(), static_cast<size_t>(width) * 4u, height,
                         D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON);
    r->motionIsZero = true;
    r->zeroDepth.assign(static_cast<size_t>(width) * height, 0.0f);
    _d3d.UploadTexture2D(r->depth.Get(), r->zeroDepth.data(), static_cast<size_t>(width) * sizeof(float), height,
                         D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON);
    r->depthIsZero = true;

    _resources = std::move(r);
    CreateFeature(*_resources);
}

bool DlssNrRunner::SetEvaluateParameters(Resources& r, bool reset, float mvecScaleX, float mvecScaleY, DWORD* sehCode) noexcept {
    *sehCode = 0;
    __try {
        _parameters->Set(P_COLOR, r.color.Get());
        _parameters->Set(P_OUTPUT, r.output.Get());
        _parameters->Set(P_MVEC, r.motion.Get());
        _parameters->Set(P_DEPTH, r.depth.Get());
        for (const auto& subrect : kSubrects) SetSubrect(_parameters, subrect, r.width, r.height);
        _parameters->Set(P_MVEC_SCALE_X, mvecScaleX);
        _parameters->Set(P_MVEC_SCALE_Y, mvecScaleY);
        _parameters->Set(P_DEPTH_INVERTED, _settings.depthInverted ? 1 : 0);
        _parameters->Set(P_INDICATOR_INVERT_X, 0);
        _parameters->Set(P_INDICATOR_INVERT_Y, 0);
        _parameters->Set(P_ENABLED, 1);
        _parameters->Set(P_RESET, reset ? 1 : 0);
        _parameters->Set(P_STYLE, _settings.style);
        _parameters->Set(P_INTENSITY, _settings.intensity);
        _parameters->Set(P_LOCAL_TONE, _settings.localToneStrength);
        _parameters->Set(P_LOCAL_STRUCTURE, _settings.localStructureStrength);
        _parameters->Set(P_SKIN_STRUCTURE, _settings.skinStructureStrength);
        _parameters->Set(P_AUTO_MASK, _settings.autoMask ? 1 : 0);
        _parameters->Set(P_UI_CORRECTION, _settings.uiCorrection ? 1 : 0);
        return true;
    } __except (CaptureNgxException(GetExceptionCode(), sehCode)) {
        return false;
    }
}

void DlssNrRunner::Evaluate(Resources& r, bool reset, float mvecScaleX, float mvecScaleY) {
    _d3d.ResetList();
    D3D12Context::Transition(_d3d.List(), r.color.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    D3D12Context::Transition(_d3d.List(), r.motion.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    D3D12Context::Transition(_d3d.List(), r.depth.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    D3D12Context::Transition(_d3d.List(), r.output.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    DWORD sehCode = 0;
    if (!SetEvaluateParameters(r, reset, mvecScaleX, mvecScaleY, &sehCode)) {
        char buf[128]{};
        sprintf_s(buf, "DLSSNR evaluation parameter setup raised SEH 0x%08X", static_cast<unsigned>(sehCode));
        throw std::runtime_error(buf);
    }
    sehCode = 0;
    const auto result = SafeEvaluateFeature(
        _evaluateFeature, _d3d.List(), _feature, _parameters, &sehCode);
    if (sehCode) {
        char buf[128]{};
        sprintf_s(buf, "DLSSNR EvaluateFeature raised SEH 0x%08X", static_cast<unsigned>(sehCode));
        throw std::runtime_error(buf);
    }
    CheckNgx(result, "DLSSNR Feature 18 EvaluateFeature");

    D3D12Context::Transition(_d3d.List(), r.color.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    D3D12Context::Transition(_d3d.List(), r.motion.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    D3D12Context::Transition(_d3d.List(), r.depth.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    D3D12Context::Transition(_d3d.List(), r.output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    _d3d.ExecuteAndWait();
}

void DlssNrRunner::EvaluateRecorded(Resources& r, bool reset, float mvecScaleX, float mvecScaleY) {
    DWORD sehCode = 0;
    if (!SetEvaluateParameters(r, reset, mvecScaleX, mvecScaleY, &sehCode)) {
        char buf[128]{};
        sprintf_s(buf, "DLSSNR evaluation parameter setup raised SEH 0x%08X", static_cast<unsigned>(sehCode));
        throw std::runtime_error(buf);
    }
    sehCode = 0;
    const auto result = SafeEvaluateFeature(
        _evaluateFeature, _d3d.List(), _feature, _parameters, &sehCode);
    if (sehCode) {
        char buf[128]{};
        sprintf_s(buf, "DLSSNR EvaluateFeature raised SEH 0x%08X", static_cast<unsigned>(sehCode));
        throw std::runtime_error(buf);
    }
    CheckNgx(result, "DLSSNR Feature 18 EvaluateFeature");
}

Rgba8Image DlssNrRunner::ProcessInternal(const Rgba8Image& input, const std::vector<float>* depth,
                                            const std::vector<float>* motionXY, bool reset,
                                            float mvecScaleX, float mvecScaleY, uint32_t evaluations) {
    if (_performanceBatching) {
        return ProcessInternalBatched(input, depth, motionXY, reset, mvecScaleX, mvecScaleY, evaluations);
    }
    return ProcessInternalLegacy(input, depth, motionXY, reset, mvecScaleX, mvecScaleY, evaluations);
}

Rgba8Image DlssNrRunner::ProcessInternalBatched(const Rgba8Image& input, const std::vector<float>* depth,
                                                 const std::vector<float>* motionXY, bool reset,
                                                 float mvecScaleX, float mvecScaleY, uint32_t evaluations) {
    if (!input.width || !input.height || input.pixels.size() != static_cast<size_t>(input.width) * input.height * 4u) {
        throw std::runtime_error("Invalid input image");
    }
    if (evaluations == 0) evaluations = 1;

    EnsureResources(input.width, input.height);
    Resources& r = *_resources;

    // Fill persistent upload heaps on the CPU first. No GPU fence is needed because the previous
    // frame is complete before ProcessInternalBatched returns.
    _d3d.WriteUploadTransferBuffer(r.colorUpload, input.pixels.data(), static_cast<size_t>(r.width) * 4u, r.height);

    bool uploadDepth = false;
    if (depth) {
        if (depth->size() != static_cast<size_t>(r.width) * r.height) {
            throw std::runtime_error("Depth map dimensions do not match input image");
        }
        _d3d.WriteUploadTransferBuffer(r.depthUpload, depth->data(), static_cast<size_t>(r.width) * sizeof(float), r.height);
        uploadDepth = true;
        r.depthIsZero = false;
    } else if (!r.depthIsZero) {
        _d3d.WriteUploadTransferBuffer(r.depthUpload, r.zeroDepth.data(), static_cast<size_t>(r.width) * sizeof(float), r.height);
        uploadDepth = true;
        r.depthIsZero = true;
    }

    bool uploadMotion = false;
    if (motionXY) {
        const size_t expected = static_cast<size_t>(r.width) * r.height * 2u;
        if (motionXY->size() != expected) throw std::runtime_error("Motion-vector dimensions do not match input image");
        if (r.packedMotion.size() != expected) r.packedMotion.resize(expected);
        video::perf::ParallelForRows(r.height, 48u, [&](uint32_t rowBegin, uint32_t rowEnd, unsigned) {
            for (uint32_t y = rowBegin; y < rowEnd; ++y) {
                const size_t first = static_cast<size_t>(y) * r.width * 2u;
                const size_t last = first + static_cast<size_t>(r.width) * 2u;
                for (size_t i = first; i < last; ++i) r.packedMotion[i] = FloatToHalf((*motionXY)[i]);
            }
        });
        _d3d.WriteUploadTransferBuffer(r.motionUpload, r.packedMotion.data(), static_cast<size_t>(r.width) * 4u, r.height);
        uploadMotion = true;
        r.motionIsZero = false;
    } else if (!r.motionIsZero) {
        _d3d.WriteUploadTransferBuffer(r.motionUpload, r.zeroMotion.data(), static_cast<size_t>(r.width) * 4u, r.height);
        uploadMotion = true;
        r.motionIsZero = true;
    }

    // One command list contains input copies, DLSSNR Evaluate and output readback. This removes
    // the V0.6.2 per-upload fence waits and reduces the hot path to one queue submit + one fence.
    _d3d.ResetList();
    _d3d.RecordUploadTexture2D(r.color.Get(), r.colorUpload,
                               D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (uploadDepth) {
        _d3d.RecordUploadTexture2D(r.depth.Get(), r.depthUpload,
                                   D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    } else {
        D3D12Context::Transition(_d3d.List(), r.depth.Get(), D3D12_RESOURCE_STATE_COMMON,
                                 D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    if (uploadMotion) {
        _d3d.RecordUploadTexture2D(r.motion.Get(), r.motionUpload,
                                   D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    } else {
        D3D12Context::Transition(_d3d.List(), r.motion.Get(), D3D12_RESOURCE_STATE_COMMON,
                                 D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    D3D12Context::Transition(_d3d.List(), r.output.Get(), D3D12_RESOURCE_STATE_COMMON,
                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    for (uint32_t i = 0; i < evaluations; ++i) {
        EvaluateRecorded(r, i == 0 ? reset : false, mvecScaleX, mvecScaleY);
        if (i + 1u < evaluations) {
            // Explicitly order repeated UAV writes when image mode requests multiple iterations.
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            barrier.UAV.pResource = r.output.Get();
            _d3d.List()->ResourceBarrier(1, &barrier);
        }
    }

    D3D12Context::Transition(_d3d.List(), r.color.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                             D3D12_RESOURCE_STATE_COMMON);
    D3D12Context::Transition(_d3d.List(), r.depth.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                             D3D12_RESOURCE_STATE_COMMON);
    D3D12Context::Transition(_d3d.List(), r.motion.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                             D3D12_RESOURCE_STATE_COMMON);
    _d3d.RecordReadbackTexture2D(r.output.Get(), r.outputReadback,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    _d3d.ExecuteAndWait();

    Rgba8Image out;
    out.width = r.width;
    out.height = r.height;
    out.pixels = _d3d.ReadTransferBuffer(r.outputReadback, r.output.Get(), 4u);
    video::perf::ParallelForRows(r.height, 64u, [&](uint32_t rowBegin, uint32_t rowEnd, unsigned) {
        for (uint32_t y = rowBegin; y < rowEnd; ++y) {
            size_t i = (static_cast<size_t>(y) * r.width) * 4u + 3u;
            const size_t end = (static_cast<size_t>(y + 1u) * r.width) * 4u;
            for (; i < end; i += 4u) out.pixels[i] = input.pixels[i];
        }
    });
    return out;
}

Rgba8Image DlssNrRunner::ProcessInternalLegacy(const Rgba8Image& input, const std::vector<float>* depth,
                                                  const std::vector<float>* motionXY, bool reset,
                                                  float mvecScaleX, float mvecScaleY, uint32_t evaluations) {
    if (!input.width || !input.height || input.pixels.size() != static_cast<size_t>(input.width) * input.height * 4u) {
        throw std::runtime_error("Invalid input image");
    }
    if (evaluations == 0) evaluations = 1;

    EnsureResources(input.width, input.height);
    Resources& r = *_resources;

    _d3d.UploadTexture2D(r.color.Get(), input.pixels.data(), static_cast<size_t>(r.width) * 4u, r.height,
                         D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON);

    if (depth) {
        if (depth->size() != static_cast<size_t>(r.width) * r.height) {
            throw std::runtime_error("Depth map dimensions do not match input image");
        }
        _d3d.UploadTexture2D(r.depth.Get(), depth->data(), static_cast<size_t>(r.width) * sizeof(float), r.height,
                             D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON);
        r.depthIsZero = false;
    } else if (!r.depthIsZero) {
        _d3d.UploadTexture2D(r.depth.Get(), r.zeroDepth.data(), static_cast<size_t>(r.width) * sizeof(float), r.height,
                             D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON);
        r.depthIsZero = true;
    }

    if (motionXY) {
        const size_t expected = static_cast<size_t>(r.width) * r.height * 2u;
        if (motionXY->size() != expected) throw std::runtime_error("Motion-vector dimensions do not match input image");
        std::vector<uint16_t> packed(expected);
        for (size_t i = 0; i < expected; ++i) packed[i] = FloatToHalf((*motionXY)[i]);
        _d3d.UploadTexture2D(r.motion.Get(), packed.data(), static_cast<size_t>(r.width) * 4u, r.height,
                             D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON);
        r.motionIsZero = false;
    } else if (!r.motionIsZero) {
        _d3d.UploadTexture2D(r.motion.Get(), r.zeroMotion.data(), static_cast<size_t>(r.width) * 4u, r.height,
                             D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON);
        r.motionIsZero = true;
    }

    for (uint32_t i = 0; i < evaluations; ++i) {
        Evaluate(r, i == 0 ? reset : false, mvecScaleX, mvecScaleY);
    }

    Rgba8Image out;
    out.width = r.width;
    out.height = r.height;
    out.pixels = _d3d.ReadbackTexture2D(r.output.Get(), 4u);
    for (size_t i = 3; i < out.pixels.size(); i += 4) out.pixels[i] = input.pixels[i];
    return out;
}

Rgba8Image DlssNrRunner::Process(const Rgba8Image& input, const std::vector<float>* depth) {
    // Image/legacy behavior remains unchanged: the first evaluation resets history,
    // additional same-image iterations can reuse it.
    return ProcessInternal(input, depth, nullptr, true, 1.0f, 1.0f, _settings.iterations);
}

Rgba8Image DlssNrRunner::ProcessTemporal(const Rgba8Image& input, const std::vector<float>* depth,
                                          const std::vector<float>* motionXY, bool reset,
                                          float mvecScaleX, float mvecScaleY) {
    // A real source frame advances temporal history exactly once. Re-evaluating the
    // same frame multiple times corrupts temporal interpretation, so iterations are
    // intentionally ignored on this path.
    return ProcessInternal(input, depth, motionXY, reset, mvecScaleX, mvecScaleY, 1u);
}

void DlssNrRunner::Shutdown() noexcept {
    ReleaseFeatureHandle();
    _resources.reset();
    if (_parameters) {
        DWORD sehCode = 0;
        const auto result = SafeDestroyParameters(_parameters, &sehCode);
        if (sehCode) {
            std::cerr << "[NGX] DestroyParameters SEH=0x" << std::hex << sehCode << std::dec << "\n";
        } else if (!NVSDK_NGX_SUCCEED(result)) {
            std::cerr << "[NGX] DestroyParameters failed: 0x" << std::hex
                      << static_cast<unsigned>(result) << std::dec << "\n";
        }
        _parameters = nullptr;
    }
    if (_snippetInitialized && _snippetShutdown) {
        DWORD sehCode = 0;
        const auto result = SafeShutdown(_snippetShutdown, _d3d.Device(), &sehCode);
        if (sehCode) {
            std::cerr << "[DLSSNR] Shutdown1 SEH=0x" << std::hex << sehCode << std::dec << "\n";
        } else if (!NVSDK_NGX_SUCCEED(result)) {
            std::cerr << "[DLSSNR] Shutdown1 failed: 0x" << std::hex
                      << static_cast<unsigned>(result) << std::dec << "\n";
        }
        _snippetInitialized = false;
    }
    _callerCompat.Uninstall();
    if (_snippetModule) {
        FreeLibrary(_snippetModule);
        _snippetModule = nullptr;
    }
    if (_coreInitialized) {
        DWORD sehCode = 0;
        const auto result = SafeCoreShutdown(_d3d.Device(), &sehCode);
        if (sehCode) {
            std::cerr << "[NGX] Core Shutdown1 SEH=0x" << std::hex << sehCode << std::dec << "\n";
        } else if (!NVSDK_NGX_SUCCEED(result)) {
            std::cerr << "[NGX] Core Shutdown1 failed: 0x" << std::hex
                      << static_cast<unsigned>(result) << std::dec << "\n";
        }
        _coreInitialized = false;
    }
}
