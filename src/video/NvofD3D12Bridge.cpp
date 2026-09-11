#include "NvofD3D12Bridge.h"
#include "NvofFlowSession.h"
#include <Windows.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef DLSS5_HAS_NVOF_SDK
#define DLSS5_HAS_NVOF_SDK 0
#endif
#ifndef DLSS5_NVOF_D3D12_BRIDGE_READY
#define DLSS5_NVOF_D3D12_BRIDGE_READY 0
#endif

#if DLSS5_HAS_NVOF_SDK && DLSS5_NVOF_D3D12_BRIDGE_READY
#include <nvOpticalFlowD3D12.h>
#endif

namespace video {

#if DLSS5_HAS_NVOF_SDK && DLSS5_NVOF_D3D12_BRIDGE_READY
namespace {
using Microsoft::WRL::ComPtr;

const char* StatusName(NV_OF_STATUS s) noexcept {
    switch (s) {
    case NV_OF_SUCCESS: return "NV_OF_SUCCESS";
    case NV_OF_ERR_OF_NOT_AVAILABLE: return "NV_OF_ERR_OF_NOT_AVAILABLE";
    case NV_OF_ERR_UNSUPPORTED_DEVICE: return "NV_OF_ERR_UNSUPPORTED_DEVICE";
    case NV_OF_ERR_DEVICE_DOES_NOT_EXIST: return "NV_OF_ERR_DEVICE_DOES_NOT_EXIST";
    case NV_OF_ERR_INVALID_PTR: return "NV_OF_ERR_INVALID_PTR";
    case NV_OF_ERR_INVALID_PARAM: return "NV_OF_ERR_INVALID_PARAM";
    case NV_OF_ERR_INVALID_CALL: return "NV_OF_ERR_INVALID_CALL";
    case NV_OF_ERR_INVALID_VERSION: return "NV_OF_ERR_INVALID_VERSION";
    case NV_OF_ERR_OUT_OF_MEMORY: return "NV_OF_ERR_OUT_OF_MEMORY";
    case NV_OF_ERR_NOT_INITIALIZED: return "NV_OF_ERR_NOT_INITIALIZED";
    case NV_OF_ERR_UNSUPPORTED_FEATURE: return "NV_OF_ERR_UNSUPPORTED_FEATURE";
    case NV_OF_ERR_GENERIC: return "NV_OF_ERR_GENERIC";
    default: return "NV_OF_STATUS_UNKNOWN";
    }
}

std::string DxgiName(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_UNORM: return "DXGI_FORMAT_R8G8B8A8_UNORM";
    case DXGI_FORMAT_B8G8R8A8_UNORM: return "DXGI_FORMAT_B8G8R8A8_UNORM";
    case DXGI_FORMAT_R16G16_SINT: return "DXGI_FORMAT_R16G16_SINT";
    case DXGI_FORMAT_R8_UINT: return "DXGI_FORMAT_R8_UINT";
    case DXGI_FORMAT_R8_UNORM: return "DXGI_FORMAT_R8_UNORM";
    case DXGI_FORMAT_NV12: return "DXGI_FORMAT_NV12";
    default: return "DXGI_FORMAT(" + std::to_string(static_cast<unsigned>(f)) + ")";
    }
}

bool Contains(const std::vector<DXGI_FORMAT>& formats, DXGI_FORMAT value) {
    return std::find(formats.begin(), formats.end(), value) != formats.end();
}

NV_OF_PERF_LEVEL ToPerf(NvofQuality quality) noexcept {
    switch (quality) {
    case NvofQuality::Fast: return NV_OF_PERF_LEVEL_FAST;
    case NvofQuality::Medium: return NV_OF_PERF_LEVEL_MEDIUM;
    case NvofQuality::Slow: default: return NV_OF_PERF_LEVEL_SLOW;
    }
}

NV_OF_OUTPUT_VECTOR_GRID_SIZE ToGrid(uint32_t grid) {
    switch (grid) {
    case 1: return NV_OF_OUTPUT_VECTOR_GRID_SIZE_1;
    case 2: return NV_OF_OUTPUT_VECTOR_GRID_SIZE_2;
    case 4: return NV_OF_OUTPUT_VECTOR_GRID_SIZE_4;
    default: throw std::runtime_error("NVOF output grid must be 1, 2, or 4");
    }
}

using PFN_CREATE_INSTANCE_D3D12 = NV_OF_STATUS(NVOFAPI*)(uint32_t, NV_OF_D3D12_API_FUNCTION_LIST*);

} // namespace

struct NvofD3D12Bridge::Impl {
    D3D12Context& d3d;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t grid = 4;
    uint32_t gridWidth = 0;
    uint32_t gridHeight = 0;
    bool outputCostEnabled = true;
    bool shuttingDown = false;
    DXGI_FORMAT inputTextureFormat = DXGI_FORMAT_UNKNOWN;
    bool inputNeedsRgbaToBgraSwizzle = false;
    std::vector<uint8_t> currentUploadScratch;
    std::vector<uint8_t> previousUploadScratch;
    HMODULE module = nullptr;
    NV_OF_D3D12_API_FUNCTION_LIST api{};
    NvOFHandle handle = nullptr;
    ComPtr<ID3D12Fence> syncFence;
    HANDLE fenceEvent = nullptr;
    uint64_t fenceValue = 0;

    ComPtr<ID3D12Resource> currentTexture;
    ComPtr<ID3D12Resource> previousTexture;
    ComPtr<ID3D12Resource> flowTexture;
    ComPtr<ID3D12Resource> costTexture;
    NvOFGPUBufferHandle currentHandle = nullptr;
    NvOFGPUBufferHandle previousHandle = nullptr;
    NvOFGPUBufferHandle flowHandle = nullptr;
    NvOFGPUBufferHandle costHandle = nullptr;

    Impl(D3D12Context& d, uint32_t w, uint32_t h, const NvofSettings& settings)
        : d3d(d), width(w), height(h), grid(settings.outputGridSize),
          outputCostEnabled(settings.outputCost) {
        if (!width || !height || !d3d.Device() || !d3d.Queue()) {
            throw std::runtime_error("NVOF D3D12 bridge received an invalid D3D12 context/dimensions");
        }
        // Validate before any driver allocation so malformed persisted UI settings cannot
        // create a partially initialized NVOF session.
        (void)ToGrid(grid);
        gridWidth = (width + grid - 1u) / grid;
        gridHeight = (height + grid - 1u) / grid;
        try {
            OpenRuntime();
            CreateSession();
            CheckGridSupport();
            InitSession(settings.quality);
            CreateFence();
            CreateAndRegisterResources();
        } catch (...) {
            // A throwing C++ constructor does not run ~Impl(). Explicitly unwind any driver
            // session/resources already created so an unsupported user-selected grid or a
            // registration error does not leak NVOF state into the next conversion.
            Shutdown();
            throw;
        }
    }

    ~Impl() noexcept { Shutdown(); }

    // Driver/resource teardown is deliberately idempotent and noexcept. Alpha2 initially
    // destroyed the NVOF handle before the registered ID3D12Resource COM objects were
    // released. NVIDIA's D3D12 cleanup order is: wait -> unregister -> free resources ->
    // NvOFDestroy -> destroy D3D12 device. Keeping that order also avoids a crash seen both
    // after normal conversion completion and after cancellation on the RTX 4090 Laptop.
    void Shutdown() noexcept {
        if (shuttingDown) return;
        shuttingDown = true;

        bool gpuIdle = true;
        try {
            WaitFor(fenceValue);
            // Drain our application's D3D12 queue as well. NVOFA uses its own fence, while
            // upload/readback work is submitted through D3D12Context's direct queue.
            d3d.Flush();
        } catch (...) {
            gpuIdle = false;
        }

        // Never tear registered resources out from under in-flight NVOFA work. In the rare
        // device-lost/timeout case it is safer to leave driver-owned state for process cleanup
        // than to force an unsafe unregister sequence from a destructor.
        if (gpuIdle && handle) {
            const bool unregistered = Unregister(flowHandle) &&
                                      Unregister(costHandle) &&
                                      Unregister(currentHandle) &&
                                      Unregister(previousHandle);
            if (!unregistered) {
                // An unregister failure means the driver may still own one or more client
                // resources. Do not continue with COM release / NvOFDestroy / FreeLibrary.
                // Leak this failed session rather than turning a recoverable driver error into
                // an access violation during worker teardown.
                (void)currentTexture.Detach();
                (void)previousTexture.Detach();
                (void)flowTexture.Detach();
                (void)costTexture.Detach();
                (void)syncFence.Detach();
                fenceEvent = nullptr;
                module = nullptr;
                return;
            }

            // The SDK programming guide explicitly frees client D3D12 resources after
            // unregistering them and before NvOFDestroy.
            flowTexture.Reset();
            costTexture.Reset();
            currentTexture.Reset();
            previousTexture.Reset();
            currentUploadScratch.clear();
            previousUploadScratch.clear();

            if (api.nvOFDestroy) {
                const NV_OF_STATUS destroyStatus = api.nvOFDestroy(handle);
                if (destroyStatus != NV_OF_SUCCESS) {
                    // Preserve runtime/synchronization objects if the driver declines to
                    // destroy the session. This path is intentionally leak-safe rather than
                    // crash-prone; normal NVOF teardown returns NV_OF_SUCCESS.
                    handle = nullptr;
                    (void)syncFence.Detach();
                    fenceEvent = nullptr;
                    module = nullptr;
                    return;
                }
                handle = nullptr;
            } else {
                handle = nullptr;
            }
        } else if (!gpuIdle) {
            // Do not release COM resources, the fence, event, session, or DLL while the driver
            // may still be touching them. Intentionally detach/leak only on catastrophic GPU
            // failure; normal completion/cancellation always follows the ordered path above.
            (void)currentTexture.Detach();
            (void)previousTexture.Detach();
            (void)flowTexture.Detach();
            (void)costTexture.Detach();
            (void)syncFence.Detach();
            fenceEvent = nullptr;
            module = nullptr;
            return;
        }

        syncFence.Reset();
        if (fenceEvent) CloseHandle(fenceEvent);
        fenceEvent = nullptr;
        if (module) FreeLibrary(module);
        module = nullptr;
    }

    [[noreturn]] void ThrowNv(const char* operation, NV_OF_STATUS s) const {
        std::ostringstream oss;
        oss << operation << " failed: " << StatusName(s) << " (" << static_cast<int>(s) << ")";
        if (handle && api.nvOFGetLastError) {
            char buffer[1024]{};
            uint32_t size = static_cast<uint32_t>(sizeof(buffer));
            if (api.nvOFGetLastError(handle, buffer, &size) == NV_OF_SUCCESS && buffer[0]) {
                oss << " | driver: " << buffer;
            }
        }
        throw std::runtime_error(oss.str());
    }

    void Check(const char* operation, NV_OF_STATUS s) const {
        if (s != NV_OF_SUCCESS) ThrowNv(operation, s);
    }

    void OpenRuntime() {
        module = LoadLibraryExW(L"nvofapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) module = LoadLibraryW(L"nvofapi64.dll");
        if (!module) throw std::runtime_error("nvofapi64.dll could not be loaded");
        auto createApi = reinterpret_cast<PFN_CREATE_INSTANCE_D3D12>(GetProcAddress(module, "NvOFAPICreateInstanceD3D12"));
        if (!createApi) throw std::runtime_error("NvOFAPICreateInstanceD3D12 export is missing");
        Check("NvOFAPICreateInstanceD3D12", createApi(NV_OF_API_VERSION, &api));
        if (!api.nvCreateOpticalFlowD3D12 || !api.nvOFInit || !api.nvOFGetSurfaceFormatCountD3D12 ||
            !api.nvOFGetSurfaceFormatD3D12 || !api.nvOFRegisterResourceD3D12 ||
            !api.nvOFUnregisterResourceD3D12 || !api.nvOFExecuteD3D12 || !api.nvOFDestroy || !api.nvOFGetCaps) {
            throw std::runtime_error("NVOF D3D12 function list is incomplete for SDK 5.x");
        }
    }

    void CreateSession() {
        Check("nvCreateOpticalFlowD3D12", api.nvCreateOpticalFlowD3D12(d3d.Device(), &handle));
        if (!handle) throw std::runtime_error("nvCreateOpticalFlowD3D12 returned a null handle");
    }

    std::vector<uint32_t> QueryCaps(NV_OF_CAPS cap) {
        uint32_t count = 0;
        Check("nvOFGetCaps(count)", api.nvOFGetCaps(handle, cap, nullptr, &count));
        std::vector<uint32_t> values(count);
        if (count) Check("nvOFGetCaps(values)", api.nvOFGetCaps(handle, cap, values.data(), &count));
        values.resize(count);
        return values;
    }

    void CheckGridSupport() {
        const auto grids = QueryCaps(NV_OF_CAPS_SUPPORTED_OUTPUT_GRID_SIZES);
        const auto requested = static_cast<uint32_t>(ToGrid(grid));
        if (std::find(grids.begin(), grids.end(), requested) == grids.end()) {
            std::ostringstream oss;
            oss << "NVOF device/driver does not report " << grid << "x" << grid
                << " optical-flow output grid support";
            throw std::runtime_error(oss.str());
        }
    }

    void InitSession(NvofQuality quality) {
        NV_OF_INIT_PARAMS p{};
        p.width = width;
        p.height = height;
        p.outGridSize = ToGrid(grid);
        p.hintGridSize = NV_OF_HINT_VECTOR_GRID_SIZE_UNDEFINED;
        p.mode = NV_OF_MODE_OPTICALFLOW;
        p.perfLevel = ToPerf(quality);
        p.enableExternalHints = NV_OF_FALSE;
        p.enableOutputCost = outputCostEnabled ? NV_OF_TRUE : NV_OF_FALSE;
        p.hPrivData = nullptr;
        p.disparityRange = NV_OF_STEREO_DISPARITY_RANGE_UNDEFINED;
        p.enableRoi = NV_OF_FALSE;
        p.predDirection = NV_OF_PRED_DIRECTION_FORWARD;
        p.enableGlobalFlow = NV_OF_FALSE;
        p.inputBufferFormat = NV_OF_BUFFER_FORMAT_ABGR8;
        Check("nvOFInit", api.nvOFInit(handle, &p));
    }

    std::vector<DXGI_FORMAT> SurfaceFormats(NV_OF_BUFFER_USAGE usage) {
        uint32_t count = 0;
        Check("nvOFGetSurfaceFormatCountD3D12", api.nvOFGetSurfaceFormatCountD3D12(handle, usage, NV_OF_MODE_OPTICALFLOW, &count));
        if (!count) throw std::runtime_error("NVOF reported no D3D12 surface formats for requested buffer usage");
        std::vector<DXGI_FORMAT> formats(count);
        Check("nvOFGetSurfaceFormatD3D12", api.nvOFGetSurfaceFormatD3D12(handle, usage, NV_OF_MODE_OPTICALFLOW, formats.data()));
        return formats;
    }

    DXGI_FORMAT RequireFormat(NV_OF_BUFFER_USAGE usage, DXGI_FORMAT required, const char* what) {
        const auto formats = SurfaceFormats(usage);
        if (Contains(formats, required)) return required;
        std::ostringstream oss;
        oss << "NVOF D3D12 " << what << " format " << DxgiName(required) << " is unavailable; driver reports: ";
        for (size_t i = 0; i < formats.size(); ++i) {
            if (i) oss << ", ";
            oss << DxgiName(formats[i]);
        }
        throw std::runtime_error(oss.str());
    }

    void CreateFence() {
        const HRESULT hr = d3d.Device()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&syncFence));
        if (FAILED(hr)) throw std::runtime_error("NVOF CreateFence failed: HRESULT=" + std::to_string(static_cast<unsigned long>(hr)));
        fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!fenceEvent) throw std::runtime_error("NVOF CreateEventW failed");
    }

    void WaitFor(uint64_t value) {
        if (!syncFence || value == 0 || syncFence->GetCompletedValue() >= value) return;
        const HRESULT hr = syncFence->SetEventOnCompletion(value, fenceEvent);
        if (FAILED(hr)) throw std::runtime_error("NVOF SetEventOnCompletion failed: HRESULT=" + std::to_string(static_cast<unsigned long>(hr)));
        const DWORD wr = WaitForSingleObject(fenceEvent, INFINITE);
        if (wr != WAIT_OBJECT_0) throw std::runtime_error("NVOF fence wait failed");
    }

    NvOFGPUBufferHandle Register(ID3D12Resource* resource) {
        NvOFGPUBufferHandle gpuHandle = nullptr;
        NV_OF_REGISTER_RESOURCE_PARAMS_D3D12 p{};
        p.resource = resource;
        p.inputFencePoint.fence = syncFence.Get();
        p.inputFencePoint.value = fenceValue;
        p.hOFGpuBuffer = &gpuHandle;
        p.outputFencePoint.fence = syncFence.Get();
        p.outputFencePoint.value = ++fenceValue;
        Check("nvOFRegisterResourceD3D12", api.nvOFRegisterResourceD3D12(handle, &p));
        WaitFor(fenceValue);
        if (!gpuHandle) throw std::runtime_error("nvOFRegisterResourceD3D12 returned a null GPU buffer handle");
        return gpuHandle;
    }

    bool Unregister(NvOFGPUBufferHandle& gpuHandle) noexcept {
        if (!gpuHandle) return true;
        if (!api.nvOFUnregisterResourceD3D12) return false;
        NV_OF_UNREGISTER_RESOURCE_PARAMS_D3D12 p{};
        p.hOFGpuBuffer = gpuHandle;
        const NV_OF_STATUS status = api.nvOFUnregisterResourceD3D12(&p);
        if (status != NV_OF_SUCCESS) return false;
        gpuHandle = nullptr;
        return true;
    }

    void CreateAndRegisterResources() {
        // Do not hard-code the DXGI mapping for NV_OF_BUFFER_FORMAT_ABGR8.
        // The NVOF D3D12 API explicitly requires clients to query the formats exposed by
        // nvOFGetSurfaceFormatD3D12. On the RTX 4090 Laptop validation machine the driver
        // reports DXGI_FORMAT_B8G8R8A8_UNORM (87), NV12 and R8_UNORM for INPUT, while the
        // original alpha2 prototype incorrectly required DXGI_FORMAT_R8G8B8A8_UNORM.
        // Prefer BGRA8 when exposed (the validated SDK/driver path), but retain RGBA8 as a
        // compatibility fallback if another driver explicitly reports it.
        const auto inputFormats = SurfaceFormats(NV_OF_BUFFER_USAGE_INPUT);
        if (Contains(inputFormats, DXGI_FORMAT_B8G8R8A8_UNORM)) {
            inputTextureFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
            inputNeedsRgbaToBgraSwizzle = true;
        } else if (Contains(inputFormats, DXGI_FORMAT_R8G8B8A8_UNORM)) {
            inputTextureFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
            inputNeedsRgbaToBgraSwizzle = false;
        } else {
            std::ostringstream oss;
            oss << "NVOF D3D12 ABGR8 input has no supported 32-bit RGB DXGI surface; driver reports: ";
            for (size_t i = 0; i < inputFormats.size(); ++i) {
                if (i) oss << ", ";
                oss << DxgiName(inputFormats[i]);
            }
            throw std::runtime_error(oss.str());
        }

        const DXGI_FORMAT flowFmt = RequireFormat(NV_OF_BUFFER_USAGE_OUTPUT, DXGI_FORMAT_R16G16_SINT, "SHORT2 flow output");
        DXGI_FORMAT costFmt = DXGI_FORMAT_UNKNOWN;
        if (outputCostEnabled) {
            costFmt = DXGI_FORMAT_R8_UINT;
            const auto costs = SurfaceFormats(NV_OF_BUFFER_USAGE_COST);
            if (!Contains(costs, costFmt)) {
                if (Contains(costs, DXGI_FORMAT_R8_UNORM)) costFmt = DXGI_FORMAT_R8_UNORM;
                else throw std::runtime_error("NVOF D3D12 UINT8 cost output format is unavailable");
            }
        }

        currentTexture = d3d.CreateTexture2D(inputTextureFormat, width, height, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON);
        previousTexture = d3d.CreateTexture2D(inputTextureFormat, width, height, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON);
        if (inputNeedsRgbaToBgraSwizzle) {
            const size_t bytes = static_cast<size_t>(width) * height * 4u;
            currentUploadScratch.resize(bytes);
            previousUploadScratch.resize(bytes);
        }
        flowTexture = d3d.CreateTexture2D(flowFmt, gridWidth, gridHeight, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON);
        if (outputCostEnabled) {
            costTexture = d3d.CreateTexture2D(costFmt, gridWidth, gridHeight, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON);
        }

        currentHandle = Register(currentTexture.Get());
        previousHandle = Register(previousTexture.Get());
        flowHandle = Register(flowTexture.Get());
        if (outputCostEnabled) costHandle = Register(costTexture.Get());
    }

    NvofNativeFrameResult Execute(const Rgba8Image& current, const Rgba8Image& previous, bool disableTemporalHints) {
        const size_t expected = static_cast<size_t>(width) * height * 4u;
        if (current.width != width || current.height != height || current.pixels.size() != expected ||
            previous.width != width || previous.height != height || previous.pixels.size() != expected) {
            throw std::runtime_error("NVOF Execute frame dimensions changed");
        }

        // alpha2 correctness path: upload both source images explicitly. The application-wide
        // Rgba8Image storage is byte-ordered R,G,B,A. If the NVOF driver exposes the validated
        // DXGI_FORMAT_B8G8R8A8_UNORM input surface, swizzle only at this bridge boundary so the
        // rest of the converter remains unchanged. Later alphas can move this swizzle/conversion
        // onto the GPU-resident decode path.
        const uint8_t* currentUpload = current.pixels.data();
        const uint8_t* previousUpload = previous.pixels.data();
        if (inputNeedsRgbaToBgraSwizzle) {
            auto swizzle = [](const std::vector<uint8_t>& rgba, std::vector<uint8_t>& bgra) {
                for (size_t i = 0; i < rgba.size(); i += 4u) {
                    bgra[i + 0] = rgba[i + 2];
                    bgra[i + 1] = rgba[i + 1];
                    bgra[i + 2] = rgba[i + 0];
                    bgra[i + 3] = rgba[i + 3];
                }
            };
            swizzle(current.pixels, currentUploadScratch);
            swizzle(previous.pixels, previousUploadScratch);
            currentUpload = currentUploadScratch.data();
            previousUpload = previousUploadScratch.data();
        }

        d3d.UploadTexture2D(currentTexture.Get(), currentUpload, static_cast<size_t>(width) * 4u, height,
                            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON);
        d3d.UploadTexture2D(previousTexture.Get(), previousUpload, static_cast<size_t>(width) * 4u, height,
                            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON);

        // Signal a fence on the D3D12 queue after the uploads. The optical-flow engine waits on
        // this point, then signals doneValue when its asynchronous Execute has finished.
        const uint64_t readyValue = ++fenceValue;
        HRESULT hr = d3d.Queue()->Signal(syncFence.Get(), readyValue);
        if (FAILED(hr)) throw std::runtime_error("NVOF input queue Signal failed: HRESULT=" + std::to_string(static_cast<unsigned long>(hr)));

        NV_OF_FENCE_POINT inputReady{};
        inputReady.fence = syncFence.Get();
        inputReady.value = readyValue;
        NV_OF_EXECUTE_INPUT_PARAMS_D3D12 in{};
        in.inputFrame = currentHandle;       // NVIDIA forward = input -> reference
        in.referenceFrame = previousHandle; // therefore current -> previous for this app
        in.externalHints = nullptr;
        in.disableTemporalHints = disableTemporalHints ? NV_OF_TRUE : NV_OF_FALSE;
        in.numRois = 0;
        in.roiData = nullptr;
        in.numFencePoints = 1;
        in.fencePoint = &inputReady;

        const uint64_t doneValue = ++fenceValue;
        NV_OF_FENCE_POINT outputDone{};
        outputDone.fence = syncFence.Get();
        outputDone.value = doneValue;
        NV_OF_EXECUTE_OUTPUT_PARAMS_D3D12 out{};
        out.outputBuffer = flowHandle;
        out.outputCostBuffer = outputCostEnabled ? costHandle : nullptr;
        out.bwdOutputBuffer = nullptr;
        out.bwdOutputCostBuffer = nullptr;
        out.globalFlowBuffer = nullptr;
        out.fencePoint = &outputDone;

        Check("nvOFExecuteD3D12", api.nvOFExecuteD3D12(handle, &in, &out));
        WaitFor(doneValue);

        const auto rawFlow = d3d.ReadbackTexture2D(flowTexture.Get(), 4u,
                                                   D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON);
        std::vector<uint8_t> rawCost;
        if (outputCostEnabled) {
            rawCost = d3d.ReadbackTexture2D(costTexture.Get(), 1u,
                                            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON);
        }
        const size_t n = static_cast<size_t>(gridWidth) * gridHeight;
        if (rawFlow.size() != n * 4u || (outputCostEnabled && rawCost.size() != n)) {
            throw std::runtime_error("NVOF D3D12 readback size mismatch");
        }

        NvofNativeFrameResult result;
        result.gridWidth = gridWidth;
        result.gridHeight = gridHeight;
        result.forward.resize(n);
        static_assert(sizeof(NvofPackedVector) == sizeof(NV_OF_FLOW_VECTOR));
        std::memcpy(result.forward.data(), rawFlow.data(), rawFlow.size());
        result.forwardCost = rawCost;
        return result;
    }
};

#else
struct NvofD3D12Bridge::Impl {};
#endif

NvofD3D12Bridge::NvofD3D12Bridge(D3D12Context& d3d, uint32_t width, uint32_t height, const NvofSettings& settings) {
#if DLSS5_HAS_NVOF_SDK && DLSS5_NVOF_D3D12_BRIDGE_READY
    _impl = std::make_unique<Impl>(d3d, width, height, settings);
#else
    (void)d3d; (void)width; (void)height; (void)settings;
    throw std::runtime_error("Native NVOF D3D12 bridge was not compiled; verify Optical Flow SDK 5.x and rebuild");
#endif
}

NvofD3D12Bridge::~NvofD3D12Bridge() = default;

NvofNativeFrameResult NvofD3D12Bridge::ExecuteCurrentToPrevious(const Rgba8Image& current,
                                                                 const Rgba8Image& previous,
                                                                 bool disableTemporalHints) {
#if DLSS5_HAS_NVOF_SDK && DLSS5_NVOF_D3D12_BRIDGE_READY
    return _impl->Execute(current, previous, disableTemporalHints);
#else
    (void)current; (void)previous; (void)disableTemporalHints;
    throw std::runtime_error("Native NVOF D3D12 bridge is unavailable");
#endif
}

uint32_t NvofD3D12Bridge::GridSize() const noexcept {
#if DLSS5_HAS_NVOF_SDK && DLSS5_NVOF_D3D12_BRIDGE_READY
    return _impl ? _impl->grid : 0u;
#else
    return 0u;
#endif
}

} // namespace video
