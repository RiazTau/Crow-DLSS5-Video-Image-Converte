#include "DlssFrameGenerationRunner.h"
#include "AppPaths.h"
#include <nvsdk_ngx_helpers_dlssg_d3d.h>
#include <Windows.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using Microsoft::WRL::ComPtr;

namespace fg {
namespace {
constexpr const char* kProjectId = "1b4fbf82-8544-4a4c-bb14-d59f1b86b73a";
constexpr const char* kEngineVersion = "Crow-DLSS-Rendering-Tool-FG-0.7.2-alpha2";

void CheckNgx(NVSDK_NGX_Result result, const char* what) {
    if (!NVSDK_NGX_SUCCEED(result)) {
        char buf[256]{};
        sprintf_s(buf, "%s failed: NGX result=0x%08X", what, static_cast<unsigned>(result));
        throw std::runtime_error(buf);
    }
}

LONG CaptureNgxException(DWORD code, DWORD* sehCode) noexcept {
    *sehCode = code;
    return EXCEPTION_EXECUTE_HANDLER;
}

NVSDK_NGX_Result SafeCoreInit(const wchar_t* appData, ID3D12Device* device,
                              const NVSDK_NGX_FeatureCommonInfo* info, DWORD* seh) noexcept {
    *seh = 0;
    __try {
        return NVSDK_NGX_D3D12_Init_with_ProjectID(
            kProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, kEngineVersion,
            appData, device, info, NVSDK_NGX_Version_API);
    } __except (CaptureNgxException(GetExceptionCode(), seh)) {
        return NVSDK_NGX_Result_FAIL_PlatformError;
    }
}

NVSDK_NGX_Result SafeGetCapability(NVSDK_NGX_Parameter** params, DWORD* seh) noexcept {
    *seh = 0;
    __try { return NVSDK_NGX_D3D12_GetCapabilityParameters(params); }
    __except (CaptureNgxException(GetExceptionCode(), seh)) { return NVSDK_NGX_Result_FAIL_PlatformError; }
}

NVSDK_NGX_Result SafeDestroyParameters(NVSDK_NGX_Parameter* params, DWORD* seh) noexcept {
    *seh = 0;
    __try { return NVSDK_NGX_D3D12_DestroyParameters(params); }
    __except (CaptureNgxException(GetExceptionCode(), seh)) { return NVSDK_NGX_Result_FAIL_PlatformError; }
}

NVSDK_NGX_Result SafeShutdown(ID3D12Device* device, DWORD* seh) noexcept {
    *seh = 0;
    __try { return NVSDK_NGX_D3D12_Shutdown1(device); }
    __except (CaptureNgxException(GetExceptionCode(), seh)) { return NVSDK_NGX_Result_FAIL_PlatformError; }
}

NVSDK_NGX_Result SafeReleaseFeature(NVSDK_NGX_Handle* feature, DWORD* seh) noexcept {
    *seh = 0;
    __try { return NVSDK_NGX_D3D12_ReleaseFeature(feature); }
    __except (CaptureNgxException(GetExceptionCode(), seh)) { return NVSDK_NGX_Result_FAIL_PlatformError; }
}

NVSDK_NGX_Result SafeCreateDlssg(ID3D12GraphicsCommandList* list,
                                 NVSDK_NGX_Handle** feature,
                                 NVSDK_NGX_Parameter* params,
                                 NVSDK_NGX_DLSSG_Create_Params* create,
                                 DWORD* seh) noexcept {
    *seh = 0;
    __try { return NGX_D3D12_CREATE_DLSSG(list, 1, 1, feature, params, create); }
    __except (CaptureNgxException(GetExceptionCode(), seh)) { return NVSDK_NGX_Result_FAIL_PlatformError; }
}

NVSDK_NGX_Result SafeEvaluateDlssg(ID3D12GraphicsCommandList* list,
                                   NVSDK_NGX_Handle* feature,
                                   NVSDK_NGX_Parameter* params,
                                   NVSDK_NGX_D3D12_DLSSG_Eval_Params* eval,
                                   NVSDK_NGX_DLSSG_Opt_Eval_Params* opt,
                                   DWORD* seh) noexcept {
    *seh = 0;
    __try { return NGX_D3D12_EVALUATE_DLSSG(list, feature, params, eval, opt); }
    __except (CaptureNgxException(GetExceptionCode(), seh)) { return NVSDK_NGX_Result_FAIL_PlatformError; }
}

NVSDK_NGX_Result SafeGetI(NVSDK_NGX_Parameter* p, const char* name, int* out, DWORD* seh) noexcept {
    *seh = 0;
    __try { return NVSDK_NGX_Parameter_GetI(p, name, out); }
    __except (CaptureNgxException(GetExceptionCode(), seh)) { return NVSDK_NGX_Result_FAIL_PlatformError; }
}

NVSDK_NGX_Result SafeGetUI(NVSDK_NGX_Parameter* p, const char* name, uint32_t* out, DWORD* seh) noexcept {
    *seh = 0;
    __try { return NVSDK_NGX_Parameter_GetUI(p, name, out); }
    __except (CaptureNgxException(GetExceptionCode(), seh)) { return NVSDK_NGX_Result_FAIL_PlatformError; }
}

bool SafeSetUI(NVSDK_NGX_Parameter* p, const char* name, uint32_t value, DWORD* seh) noexcept {
    *seh = 0;
    __try { NVSDK_NGX_Parameter_SetUI(p, name, value); return true; }
    __except (CaptureNgxException(GetExceptionCode(), seh)) { return false; }
}

bool SafeSetULL(NVSDK_NGX_Parameter* p, const char* name, uint64_t value, DWORD* seh) noexcept {
    *seh = 0;
    __try { NVSDK_NGX_Parameter_SetULL(p, name, value); return true; }
    __except (CaptureNgxException(GetExceptionCode(), seh)) { return false; }
}

uint16_t FloatToHalf(float value) noexcept {
    const uint32_t bits = std::bit_cast<uint32_t>(value);
    const uint32_t sign = (bits >> 16u) & 0x8000u;
    const uint32_t mantissa = bits & 0x007FFFFFu;
    const int32_t exponent = static_cast<int32_t>((bits >> 23u) & 0xFFu) - 127;
    if (exponent == 128) return static_cast<uint16_t>(sign | (mantissa ? 0x7E00u : 0x7C00u));
    if (exponent > 15) return static_cast<uint16_t>(sign | 0x7C00u);
    if (exponent < -24) return static_cast<uint16_t>(sign);
    if (exponent < -14) {
        const uint32_t m = mantissa | 0x00800000u;
        const int shift = (-14 - exponent) + 13;
        uint32_t halfMantissa = m >> shift;
        if (shift > 0 && ((m >> (shift - 1)) & 1u)) ++halfMantissa;
        return static_cast<uint16_t>(sign | (halfMantissa & 0x03FFu));
    }
    uint32_t he = static_cast<uint32_t>(exponent + 15) << 10u;
    uint32_t hm = mantissa >> 13u;
    if (mantissa & 0x00001000u) {
        ++hm;
        if (hm & 0x400u) { hm = 0; he += 0x400u; }
    }
    return static_cast<uint16_t>(sign | std::min<uint32_t>(he, 0x7C00u) | (hm & 0x03FFu));
}

void Identity(float m[4][4]) noexcept {
    for (uint32_t r = 0; r < 4; ++r)
        for (uint32_t c = 0; c < 4; ++c)
            m[r][c] = r == c ? 1.0f : 0.0f;
}

D3D12_HEAP_PROPERTIES HeapProps(D3D12_HEAP_TYPE type) noexcept {
    D3D12_HEAP_PROPERTIES p{};
    p.Type = type;
    p.CreationNodeMask = 1;
    p.VisibleNodeMask = 1;
    return p;
}

ComPtr<ID3D12Resource> CreateBuffer(ID3D12Device* device, uint64_t bytes,
                                    D3D12_HEAP_TYPE heapType,
                                    D3D12_RESOURCE_FLAGS flags,
                                    D3D12_RESOURCE_STATES state) {
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = bytes;
    d.Height = 1;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    d.Flags = flags;
    auto heap = HeapProps(heapType);
    ComPtr<ID3D12Resource> r;
    const HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, state,
                                                       nullptr, IID_PPV_ARGS(&r));
    if (FAILED(hr)) throw std::runtime_error("CreateCommittedResource(buffer) failed");
    return r;
}

bool ReadDisableFlag(ID3D12Resource* readback) {
    uint8_t* mapped = nullptr;
    D3D12_RANGE range{0, 4};
    if (FAILED(readback->Map(0, &range, reinterpret_cast<void**>(&mapped))) || !mapped)
        throw std::runtime_error("Map DLSSG interpolation-disable readback failed");
    const bool disabled = mapped[0] != 0;
    D3D12_RANGE noWrite{0, 0};
    readback->Unmap(0, &noWrite);
    return disabled;
}
} // namespace

struct DlssFrameGenerationRunner::Resources {
    uint32_t width = 0;
    uint32_t height = 0;
    ComPtr<ID3D12Resource> color;
    ComPtr<ID3D12Resource> motion;
    ComPtr<ID3D12Resource> depth;
    ComPtr<ID3D12Resource> output;
    ComPtr<ID3D12Resource> interpolationDisable;
    ComPtr<ID3D12Resource> interpolationDisableReadback;
    D3D12Context::TextureTransferBuffer colorUpload;
    D3D12Context::TextureTransferBuffer motionUpload;
    D3D12Context::TextureTransferBuffer depthUpload;
    D3D12Context::TextureTransferBuffer outputReadback;
    std::vector<uint16_t> packedMotion;
    std::vector<float> zeroDepth;
};

DlssFrameGenerationRunner::DlssFrameGenerationRunner(D3D12Context& d3d,
                                                       std::filesystem::path runtimeDirectory,
                                                       bool manageCoreLifetime)
    : _d3d(d3d), _runtimeDirectory(std::move(runtimeDirectory)),
      _runtimeDll(_runtimeDirectory / L"nvngx_dlssg.dll"),
      _manageCoreLifetime(manageCoreLifetime) {
    if (!std::filesystem::exists(_runtimeDll)) {
        throw std::runtime_error("nvngx_dlssg.dll not found: " + _runtimeDll.string() +
            "\nRe-run BUILD.bat or copy the official DLSS Frame Generation runtime into dist\\runtime.");
    }
    if (_manageCoreLifetime) InitializeCore();
    QueryCapability();
    if (!_capability.available) {
        const std::string narrow(_capability.message.begin(), _capability.message.end());
        throw std::runtime_error(narrow.empty() ? "DLSS Frame Generation is unavailable" : narrow);
    }
}

DlssFrameGenerationRunner::~DlssFrameGenerationRunner() { Shutdown(); }

void DlssFrameGenerationRunner::InitializeCore() {
    const std::wstring appDir = app::ExecutableDir().wstring();
    const std::wstring runtimeDir = _runtimeDirectory.wstring();
    const wchar_t* paths[] = { appDir.c_str(), runtimeDir.c_str() };
    NVSDK_NGX_FeatureCommonInfo info{};
    info.PathListInfo.Path = paths;
    info.PathListInfo.Length = 2;
    DWORD seh = 0;
    const auto result = SafeCoreInit(appDir.c_str(), _d3d.Device(), &info, &seh);
    if (seh) {
        char buf[160]{}; sprintf_s(buf, "NGX Core Init raised SEH 0x%08X", static_cast<unsigned>(seh));
        throw std::runtime_error(buf);
    }
    CheckNgx(result, "NVSDK_NGX_D3D12_Init_with_ProjectID");
    _coreInitialized = true;
}

void DlssFrameGenerationRunner::QueryCapability() {
    DWORD seh = 0;
    const auto result = SafeGetCapability(&_parameters, &seh);
    if (seh) {
        char buf[160]{}; sprintf_s(buf, "NGX GetCapabilityParameters raised SEH 0x%08X", static_cast<unsigned>(seh));
        throw std::runtime_error(buf);
    }
    CheckNgx(result, "NVSDK_NGX_D3D12_GetCapabilityParameters");
    if (!_parameters) throw std::runtime_error("NGX returned a null capability parameter block");

    int available = 0;
    int initResult = 0;
    uint32_t maxGenerated = 1;
    DWORD qSeh = 0;
    const auto availableResult = SafeGetI(_parameters, NVSDK_NGX_Parameter_FrameGeneration_Available,
                                          &available, &qSeh);
    DWORD initSeh = 0;
    SafeGetI(_parameters, NVSDK_NGX_Parameter_FrameGeneration_FeatureInitResult, &initResult, &initSeh);
    DWORD maxSeh = 0;
    if (!NVSDK_NGX_SUCCEED(SafeGetUI(_parameters, NVSDK_NGX_DLSSG_Parameter_MultiFrameCountMax,
                                     &maxGenerated, &maxSeh))) {
        maxGenerated = 1;
    }

    _capability.available = NVSDK_NGX_SUCCEED(availableResult) && available != 0;
    _capability.featureInitResult = initResult;

    DXGI_ADAPTER_DESC1 adapterDesc{};
    if (_d3d.Adapter() && SUCCEEDED(_d3d.Adapter()->GetDesc1(&adapterDesc))) {
        _capability.adapterName = adapterDesc.Description;
        const std::wstring name = _capability.adapterName;
        _capability.isRtx40Series = name.find(L"RTX 40") != std::wstring::npos;
        _capability.isRtx50Series = name.find(L"RTX 50") != std::wstring::npos;
    }

    // NVIDIA product policy: RTX 40-series supports classic FG (2X) but not MFG >2X.
    // RTX 50-series is capability-driven and currently capped by Crow at 6X.
    uint32_t effectiveGenerated = std::max(1u, maxGenerated);
    if (_capability.isRtx40Series) effectiveGenerated = 1;
    if (_capability.isRtx50Series) effectiveGenerated = std::min<uint32_t>(effectiveGenerated, 5u);
    _capability.maxGeneratedFrames = effectiveGenerated;
    _capability.maxMultiplier = std::clamp<uint32_t>(effectiveGenerated + 1u, 2u, 6u);

    if (_capability.available) {
        wchar_t buf[384]{};
        swprintf_s(buf, L"DLSS Frame Generation available on %s. Maximum Crow multiplier: %uX.",
                   _capability.adapterName.empty() ? L"NVIDIA GPU" : _capability.adapterName.c_str(),
                   _capability.maxMultiplier);
        _capability.message = buf;
    } else {
        wchar_t buf[320]{};
        swprintf_s(buf, L"DLSS Frame Generation unavailable. FeatureInitResult=0x%08X, query SEH=0x%08X.",
                   static_cast<unsigned>(initResult), static_cast<unsigned>(qSeh ? qSeh : initSeh));
        _capability.message = buf;
    }
}

void DlssFrameGenerationRunner::EnsureFeature(uint32_t width, uint32_t height) {
    if (_feature && _resources && _resources->width == width && _resources->height == height) return;
    ReleaseFeature();
    _resources.reset();

    auto r = std::make_unique<Resources>();
    r->width = width; r->height = height;
    r->color = _d3d.CreateTexture2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height,
                                     D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON);
    r->motion = _d3d.CreateTexture2D(DXGI_FORMAT_R16G16_FLOAT, width, height,
                                      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON);
    r->depth = _d3d.CreateTexture2D(DXGI_FORMAT_R32_FLOAT, width, height,
                                     D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON);
    r->output = _d3d.CreateTexture2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height,
                                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                      D3D12_RESOURCE_STATE_COMMON);
    r->interpolationDisable = CreateBuffer(_d3d.Device(), 4, D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    r->interpolationDisableReadback = CreateBuffer(_d3d.Device(), 4, D3D12_HEAP_TYPE_READBACK,
        D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);

    r->colorUpload = _d3d.CreateUploadTransferBuffer(r->color.Get());
    r->motionUpload = _d3d.CreateUploadTransferBuffer(r->motion.Get());
    r->depthUpload = _d3d.CreateUploadTransferBuffer(r->depth.Get());
    r->outputReadback = _d3d.CreateReadbackTransferBuffer(r->output.Get());
    r->packedMotion.resize(static_cast<size_t>(width) * height * 2u);
    r->zeroDepth.assign(static_cast<size_t>(width) * height, 0.0f);

    // Seed static zero-depth once. It is returned to COMMON by every evaluate.
    _d3d.WriteUploadTransferBuffer(r->depthUpload, r->zeroDepth.data(),
                                   static_cast<size_t>(width) * sizeof(float), height);
    _d3d.ResetList();
    _d3d.RecordUploadTexture2D(r->depth.Get(), r->depthUpload,
                               D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON);
    _d3d.ExecuteAndWait();

    _resources = std::move(r);
    CreateFeature(width, height);
    _forceReset = true;
}

void DlssFrameGenerationRunner::CreateFeature(uint32_t width, uint32_t height) {
    const uint32_t neverProvided =
        NVSDK_NGX_DLSSG_ResourceFlags_HUDLess |
        NVSDK_NGX_DLSSG_ResourceFlags_UI |
        NVSDK_NGX_DLSSG_ResourceFlags_UIAlpha |
        NVSDK_NGX_DLSSG_ResourceFlags_BidirectionalDistortionField |
        NVSDK_NGX_DLSSG_ResourceFlags_OutputReal;
    DWORD seh = 0;
    if (!SafeSetUI(_parameters, NVSDK_NGX_DLSSG_Parameter_ResourceNeverProvided_Flags,
                   neverProvided, &seh)) {
        char buf[160]{}; sprintf_s(buf, "Set DLSSG resource flags raised SEH 0x%08X", static_cast<unsigned>(seh));
        throw std::runtime_error(buf);
    }

    NVSDK_NGX_DLSSG_Create_Params create{};
    create.Width = width;
    create.Height = height;
    create.NativeBackbufferFormat = static_cast<unsigned>(DXGI_FORMAT_R8G8B8A8_UNORM);
    create.RenderWidth = width;
    create.RenderHeight = height;
    create.DynamicResolutionScaling = false;

    _d3d.ResetList();
    const auto result = SafeCreateDlssg(_d3d.List(), &_feature, _parameters, &create, &seh);
    if (seh) {
        char buf[160]{}; sprintf_s(buf, "NGX_D3D12_CREATE_DLSSG raised SEH 0x%08X", static_cast<unsigned>(seh));
        throw std::runtime_error(buf);
    }
    CheckNgx(result, "NGX_D3D12_CREATE_DLSSG");
    if (!_feature) throw std::runtime_error("DLSSG CreateFeature returned a null feature handle");
    _d3d.ExecuteAndWait();
}

std::optional<Rgba8Image> DlssFrameGenerationRunner::GenerateFrame(
    const Rgba8Image& current, const std::vector<float>& motionXY,
    const std::vector<float>* depth, bool depthInverted,
    bool reset, uint64_t backbufferFrameId,
    uint32_t multiFrameCount, uint32_t multiFrameIndex) {
    if (!current.width || !current.height ||
        current.pixels.size() != static_cast<size_t>(current.width) * current.height * 4u) {
        throw std::runtime_error("DLSSG received an invalid RGBA frame");
    }
    if (motionXY.size() != static_cast<size_t>(current.width) * current.height * 2u) {
        throw std::runtime_error("DLSSG motion-vector size mismatch");
    }
    if (depth && depth->size() != static_cast<size_t>(current.width) * current.height) {
        throw std::runtime_error("DLSSG depth size mismatch");
    }
    if (multiFrameCount < 1 || multiFrameCount > 5 || multiFrameIndex < 1 || multiFrameIndex > multiFrameCount) {
        throw std::runtime_error("DLSSG invalid Multi Frame Generation count/index");
    }
    if (multiFrameCount > _capability.maxGeneratedFrames) {
        throw std::runtime_error("Requested DLSSG multiplier exceeds the GPU/runtime capability gate");
    }

    EnsureFeature(current.width, current.height);
    auto& r = *_resources;

    for (size_t i = 0; i < motionXY.size(); ++i) {
        const float v = std::isfinite(motionXY[i]) ? motionXY[i] : 0.0f;
        r.packedMotion[i] = FloatToHalf(v);
    }
    _d3d.WriteUploadTransferBuffer(r.colorUpload, current.pixels.data(),
                                   static_cast<size_t>(current.width) * 4u, current.height);
    _d3d.WriteUploadTransferBuffer(r.motionUpload, r.packedMotion.data(),
                                   static_cast<size_t>(current.width) * 2u * sizeof(uint16_t), current.height);
    const auto& depthSource = depth ? *depth : r.zeroDepth;
    _d3d.WriteUploadTransferBuffer(r.depthUpload, depthSource.data(),
                                   static_cast<size_t>(current.width) * sizeof(float), current.height);

    const bool resetThisFrame = reset || _forceReset;
    DWORD seh = 0;
    if (!SafeSetULL(_parameters, NVSDK_NGX_DLSSG_Parameter_BackbufferFrameID,
                    backbufferFrameId, &seh)) {
        char buf[160]{}; sprintf_s(buf, "Set DLSSG BackbufferFrameID raised SEH 0x%08X", static_cast<unsigned>(seh));
        throw std::runtime_error(buf);
    }

    _d3d.ResetList();
    _d3d.RecordUploadTexture2D(r.color.Get(), r.colorUpload,
                               D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    _d3d.RecordUploadTexture2D(r.motion.Get(), r.motionUpload,
                               D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    _d3d.RecordUploadTexture2D(r.depth.Get(), r.depthUpload,
                               D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    D3D12Context::Transition(_d3d.List(), r.output.Get(), D3D12_RESOURCE_STATE_COMMON,
                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    NVSDK_NGX_D3D12_DLSSG_Eval_Params eval{};
    eval.pBackbuffer = r.color.Get();
    eval.pDepth = r.depth.Get();
    eval.pMVecs = r.motion.Get();
    eval.pOutputInterpFrame = r.output.Get();
    eval.pOutputDisableInterpolation = r.interpolationDisable.Get();

    NVSDK_NGX_DLSSG_Opt_Eval_Params opt{};
    opt.multiFrameCount = multiFrameCount;
    opt.multiFrameIndex = multiFrameIndex;
    Identity(opt.cameraViewToClip);
    Identity(opt.clipToCameraView);
    Identity(opt.clipToLensClip);
    Identity(opt.clipToPrevClip);
    Identity(opt.prevClipToClip);
    opt.mvecScale[0] = 1.0f;
    opt.mvecScale[1] = 1.0f;
    opt.cameraUp[1] = 1.0f;
    opt.cameraRight[0] = 1.0f;
    opt.cameraFwd[2] = 1.0f;
    opt.cameraNear = 0.1f;
    opt.cameraFar = 1000.0f;
    opt.cameraFOV = 1.04719755f;
    opt.cameraAspectRatio = static_cast<float>(current.width) / static_cast<float>(current.height);
    opt.colorBuffersHDR = false;
    opt.depthInverted = depthInverted;
    opt.cameraMotionIncluded = !resetThisFrame;
    opt.reset = resetThisFrame;
    opt.automodeOverrideReset = false;
    opt.notRenderingGameFrames = false;
    opt.orthoProjection = false;
    opt.motionVectorsInvalidValue = 0.0f;
    opt.motionVectorsDilated = !resetThisFrame;
    opt.menuDetectionEnabled = false;
    opt.mvecsSubrectSize = { current.width, current.height };
    opt.depthSubrectSize = { current.width, current.height };
    opt.backbufferSubrectSize = { current.width, current.height };
    opt.outputInterpSubrectSize = { current.width, current.height };

    const auto evalResult = SafeEvaluateDlssg(_d3d.List(), _feature, _parameters, &eval, &opt, &seh);
    if (seh) {
        char buf[192]{}; sprintf_s(buf, "NGX_D3D12_EVALUATE_DLSSG raised SEH 0x%08X", static_cast<unsigned>(seh));
        throw std::runtime_error(buf);
    }
    CheckNgx(evalResult, "NGX_D3D12_EVALUATE_DLSSG");

    D3D12Context::Transition(_d3d.List(), r.interpolationDisable.Get(),
                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    _d3d.List()->CopyBufferRegion(r.interpolationDisableReadback.Get(), 0,
                                  r.interpolationDisable.Get(), 0, 4);
    D3D12Context::Transition(_d3d.List(), r.interpolationDisable.Get(),
                             D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    _d3d.RecordReadbackTexture2D(r.output.Get(), r.outputReadback,
                                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    D3D12Context::Transition(_d3d.List(), r.color.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                             D3D12_RESOURCE_STATE_COMMON);
    D3D12Context::Transition(_d3d.List(), r.motion.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                             D3D12_RESOURCE_STATE_COMMON);
    D3D12Context::Transition(_d3d.List(), r.depth.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                             D3D12_RESOURCE_STATE_COMMON);
    _d3d.ExecuteAndWait();

    // Keep reset active for all generated slots belonging to the same reset source frame.
    if (multiFrameIndex == multiFrameCount) _forceReset = false;
    const bool interpolationDisabled = ReadDisableFlag(r.interpolationDisableReadback.Get());
    if (resetThisFrame || interpolationDisabled) return std::nullopt;

    Rgba8Image generated;
    generated.width = current.width;
    generated.height = current.height;
    generated.pixels = _d3d.ReadTransferBuffer(r.outputReadback, r.output.Get(), 4);
    return generated;
}

void DlssFrameGenerationRunner::Drain() { _d3d.Flush(); }

void DlssFrameGenerationRunner::ReleaseFeature() noexcept {
    if (!_feature) return;
    try { _d3d.Flush(); } catch (...) {}
    DWORD seh = 0;
    const auto result = SafeReleaseFeature(_feature, &seh);
    if (seh || !NVSDK_NGX_SUCCEED(result)) {
        std::cerr << "[DLSSFG] ReleaseFeature warning: result=0x" << std::hex
                  << static_cast<unsigned>(result) << " seh=0x" << seh << std::dec << "\n";
    }
    _feature = nullptr;
}

void DlssFrameGenerationRunner::Shutdown() noexcept {
    ReleaseFeature();
    _resources.reset();
    if (_parameters) {
        DWORD seh = 0;
        const auto result = SafeDestroyParameters(_parameters, &seh);
        if (seh || !NVSDK_NGX_SUCCEED(result)) {
            std::cerr << "[DLSSFG] DestroyParameters warning\n";
        }
        _parameters = nullptr;
    }
    if (_manageCoreLifetime && _coreInitialized) {
        DWORD seh = 0;
        const auto result = SafeShutdown(_d3d.Device(), &seh);
        if (seh || !NVSDK_NGX_SUCCEED(result)) {
            std::cerr << "[DLSSFG] Shutdown warning\n";
        }
        _coreInitialized = false;
    }
}

} // namespace fg
