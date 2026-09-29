cmake_minimum_required(VERSION 3.24)
project(CrowDLSSRenderingTool VERSION 0.7.3 LANGUAGES C CXX)

if(NOT WIN32)
  message(FATAL_ERROR "Crow - DLSS Rendering Tool currently targets Windows x64 only.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "x64 build required.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(NGX_SDK_DIR "" CACHE PATH "Path to NVIDIA DLSS/NGX SDK root")
set(TINYEXR_DIR "" CACHE PATH "Path to TinyEXR release checkout")
set(NVOF_SDK_DIR "" CACHE PATH "Path to NVIDIA Optical Flow SDK 5.x root")
set(NVAPI_SDK_DIR "" CACHE PATH "Optional path to NVIDIA NVAPI SDK root (enables FG model preset overrides)")
option(DLSS5_PORTABLE_BUILD "Prepare binaries for app-local portable packaging" OFF)
if(NOT NGX_SDK_DIR)
  message(FATAL_ERROR "Set -DNGX_SDK_DIR=C:/path/to/NVIDIA/DLSS")
endif()
if(NOT TINYEXR_DIR)
  message(FATAL_ERROR "Set -DTINYEXR_DIR=C:/path/to/tinyexr")
endif()
find_path(NGX_INCLUDE_DIR nvsdk_ngx.h PATHS "${NGX_SDK_DIR}/include" NO_DEFAULT_PATH REQUIRED)
find_path(TINYEXR_INCLUDE_DIR tinyexr.h PATHS "${TINYEXR_DIR}" NO_DEFAULT_PATH REQUIRED)
find_path(MINIZ_INCLUDE_DIR miniz.h PATHS "${TINYEXR_DIR}/deps/miniz" NO_DEFAULT_PATH REQUIRED)
set(CROW_HAS_NVAPI 0)
set(NVAPI_INCLUDE_DIR "")
set(NVAPI_LIBRARY "")
if(NVAPI_SDK_DIR)
  # The build bootstrap supplies the NVAPI checkout root explicitly. Validate that root
  # deterministically instead of calling find_path() on a variable that was already defined.
  # This also avoids stale CMake-cache hits when the user replaces the SDK checkout.
  set(_NVAPI_HEADER "${NVAPI_SDK_DIR}/nvapi.h")
  set(_NVAPI_SETTINGS_HEADER "${NVAPI_SDK_DIR}/NvApiDriverSettings.h")
  set(_NVAPI_LIBRARY "${NVAPI_SDK_DIR}/amd64/nvapi64.lib")
  if(EXISTS "${_NVAPI_HEADER}" AND EXISTS "${_NVAPI_SETTINGS_HEADER}" AND EXISTS "${_NVAPI_LIBRARY}")
    set(NVAPI_INCLUDE_DIR "${NVAPI_SDK_DIR}")
    set(NVAPI_LIBRARY "${_NVAPI_LIBRARY}")
    set(CROW_HAS_NVAPI 1)
    message(STATUS "NVAPI SDK: ${NVAPI_SDK_DIR} (FG model preset override enabled)")
  else()
    message(WARNING "NVAPI_SDK_DIR is incomplete; FG model preset overrides will be disabled for this build: ${NVAPI_SDK_DIR}")
  endif()
else()
  message(STATUS "NVAPI SDK not supplied: building without FG model preset overrides. Driver Default remains available.")
endif()
set(MINIZ_SOURCE "${TINYEXR_DIR}/deps/miniz/miniz.c")
if(NOT EXISTS "${MINIZ_SOURCE}")
  message(FATAL_ERROR "TinyEXR miniz source not found: ${MINIZ_SOURCE}")
endif()


# NVIDIA Optical Flow SDK remains an external dependency in V0.6.6-alpha2. The NVIDIA
# driver runtime (nvofapi64.dll) is loaded dynamically; vendor SDK headers are never bundled.
unset(NVOF_INCLUDE_DIR CACHE)
if(NVOF_SDK_DIR)
  find_path(NVOF_INCLUDE_DIR nvOpticalFlowD3D12.h
    PATHS
      "${NVOF_SDK_DIR}/NvOFInterface"
      "${NVOF_SDK_DIR}/include"
      "${NVOF_SDK_DIR}"
    NO_DEFAULT_PATH)
  if(NVOF_INCLUDE_DIR)
    message(STATUS "NVOF SDK headers: ${NVOF_INCLUDE_DIR}")
  else()
    message(WARNING "NVOF_SDK_DIR was supplied but nvOpticalFlowD3D12.h was not found. native NVOF execute bridge will remain disabled.")
  endif()
else()
  set(NVOF_INCLUDE_DIR "")
endif()
if(NVOF_INCLUDE_DIR)
  set(DLSS5_HAS_NVOF_SDK 1)
else()
  set(DLSS5_HAS_NVOF_SDK 0)
endif()

# alpha2 only enables the native bridge after the selected SDK headers prove that the exact
# SDK 5.x D3D12 ABI used by our implementation is present. This prevents an older/mismatched
# header package from turning into a runtime ABI experiment.
set(DLSS5_NVOF_D3D12_BRIDGE_READY 0)
set(DLSS5_NVOF_D3D12_ABI_OK 0)
if(NVOF_INCLUDE_DIR)
  include(CheckCXXSourceCompiles)
  unset(DLSS5_NVOF_D3D12_ABI_OK CACHE)
  unset(DLSS5_NVOF_D3D12_ABI_OK)
  set(_nvof_saved_required_includes "${CMAKE_REQUIRED_INCLUDES}")
  set(CMAKE_REQUIRED_INCLUDES "${NVOF_INCLUDE_DIR}")
  check_cxx_source_compiles([[
    #include <Windows.h>
    #include <nvOpticalFlowD3D12.h>
    int main() {
      NV_OF_D3D12_API_FUNCTION_LIST api{};
      (void)api.nvCreateOpticalFlowD3D12;
      (void)api.nvOFInit;
      (void)api.nvOFGetSurfaceFormatCountD3D12;
      (void)api.nvOFGetSurfaceFormatD3D12;
      (void)api.nvOFRegisterResourceD3D12;
      (void)api.nvOFUnregisterResourceD3D12;
      (void)api.nvOFExecuteD3D12;
      (void)api.nvOFDestroy;
      (void)api.nvOFGetCaps;
      NV_OF_INIT_PARAMS init{};
      init.predDirection = NV_OF_PRED_DIRECTION_BOTH;
      init.inputBufferFormat = NV_OF_BUFFER_FORMAT_ABGR8;
      init.outGridSize = NV_OF_OUTPUT_VECTOR_GRID_SIZE_4;
      auto grid2 = NV_OF_OUTPUT_VECTOR_GRID_SIZE_2;
      auto grid1 = NV_OF_OUTPUT_VECTOR_GRID_SIZE_1;
      (void)grid2; (void)grid1;
      NV_OF_EXECUTE_INPUT_PARAMS_D3D12 in{};
      in.numFencePoints = 0; in.fencePoint = nullptr;
      NV_OF_EXECUTE_OUTPUT_PARAMS_D3D12 out{};
      out.bwdOutputBuffer = nullptr; out.bwdOutputCostBuffer = nullptr;
      out.fencePoint = nullptr;
      NV_OF_REGISTER_RESOURCE_PARAMS_D3D12 reg{};
      reg.inputFencePoint.value = 0; reg.outputFencePoint.value = 0;
      return int(NV_OF_API_MAJOR_VERSION < 5);
    }
  ]] DLSS5_NVOF_D3D12_ABI_OK)
  set(CMAKE_REQUIRED_INCLUDES "${_nvof_saved_required_includes}")
  if(DLSS5_NVOF_D3D12_ABI_OK)
    set(DLSS5_NVOF_D3D12_BRIDGE_READY 1)
  else()
    set(DLSS5_NVOF_D3D12_ABI_OK 0)
    message(WARNING "NVOF SDK headers were found but the SDK 5.x D3D12 ABI probe failed; native execute bridge disabled.")
  endif()
endif()

set(_ngx_preferred_candidates
  "${NGX_SDK_DIR}/lib/Windows_x86_64/x64/nvsdk_ngx_d.lib"
  "${NGX_SDK_DIR}/lib/Windows_x86_64/x86_64/nvsdk_ngx_d.lib"
)
set(NGX_LIBRARY "")
foreach(_candidate IN LISTS _ngx_preferred_candidates)
  if(EXISTS "${_candidate}")
    set(NGX_LIBRARY "${_candidate}")
    break()
  endif()
endforeach()
if(NOT NGX_LIBRARY)
  message(FATAL_ERROR "Could not find current x64 nvsdk_ngx_d.lib. Do not use the legacy vs2010 library.")
endif()
string(TOLOWER "${NGX_LIBRARY}" _ngx_library_lower)
if(_ngx_library_lower MATCHES "[/\\]vs2010[/\\]")
  message(FATAL_ERROR "Refusing legacy VS2010 NGX library: ${NGX_LIBRARY}")
endif()


set(VIDEO_SOURCES
  src/video/FfmpegProcess.cpp
  src/video/AutoDepthVideo.cpp
  src/video/VideoConverter.cpp
  src/video/TemporalFlow.cpp
  src/video/TemporalDenoiser.cpp
  src/video/DisFlowVideo.cpp
  src/video/SeaRaftFlowSession.cpp
  src/video/ExternalSequence.cpp
  src/video/ExternalRenderData.cpp
  src/video/ExternalDataCalibration.cpp
  src/video/ExternalRenderDataDialog.cpp
  src/video/MotionPreview.cpp
  src/video/NvofRuntimeProbe.cpp
  src/video/NvofFlowPostprocess.cpp
  src/video/TemporalMotionConsensus.cpp
  src/video/SpatialMotionConsensus.cpp
  src/video/NvofD3D12Bridge.cpp
  src/video/NvofFlowSession.cpp
)

set(CORE_SOURCES
  src/AppPaths.cpp
  src/ImageWic.cpp
  src/ImageImport.cpp
  src/ExrCodec.cpp
  src/D3D12Context.cpp
  src/DlssNrRunner.cpp
  src/SnippetCallerCompat.cpp
  src/RuntimeCompat.cpp
  src/ModelBootstrap.cpp
  src/AutoDepthRunner.cpp
  src/NgxCoreSession.cpp
  "${MINIZ_SOURCE}"
)

function(configure_dlss_target target)
  target_include_directories(${target} PRIVATE
    src
    "${NGX_INCLUDE_DIR}"
    "${TINYEXR_INCLUDE_DIR}"
    "${MINIZ_INCLUDE_DIR}"
  )
  if(NVOF_INCLUDE_DIR)
    target_include_directories(${target} PRIVATE "${NVOF_INCLUDE_DIR}")
  endif()
  target_compile_definitions(${target} PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00 TINYEXR_USE_THREAD=0 TINYEXR_USE_OPENMP=0
    _DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR
    DLSS5_HAS_NVOF_SDK=${DLSS5_HAS_NVOF_SDK}
    DLSS5_NVOF_D3D12_BRIDGE_READY=${DLSS5_NVOF_D3D12_BRIDGE_READY}
    CROW_HAS_NVAPI=${CROW_HAS_NVAPI}
  )
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc)
    # NVIDIA NGX nvsdk_ngx_d.lib is built against the dynamic MSVC runtime.
    # Portable builds therefore remain /MD and ship the VC143 CRT app-local.
    # Do NOT switch this target to /MT; V0.6.5.3 proved that it causes LNK2038/LNK2005 conflicts.
    set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
  endif()
  if(DLSS5_PORTABLE_BUILD)
    target_compile_definitions(${target} PRIVATE DLSS5_PORTABLE_BUILD=1)
  endif()
  target_link_libraries(${target} PRIVATE
    "${NGX_LIBRARY}"
    d3d12 dxgi dxguid windowscodecs ole32 shell32 shlwapi urlmon bcrypt
    user32 gdi32 comdlg32 comctl32
  )
  set_target_properties(${target} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/dist"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/dist"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/dist"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/dist"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_SOURCE_DIR}/dist"
  )
endfunction()

# Keep the dist root reserved for user-facing application EXEs.
# CLI, diagnostics and unit/self-test executables live under dist/tools.
function(configure_tool_output target)
  set_target_properties(${target} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/dist/tools"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/dist/tools"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/dist/tools"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/dist/tools"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_SOURCE_DIR}/dist/tools"
  )
endfunction()

add_executable(crow-cli src/main.cpp ${CORE_SOURCES})
configure_dlss_target(crow-cli)
set_target_properties(crow-cli PROPERTIES OUTPUT_NAME "Crow-DLSS-Rendering-Tool-CLI")
configure_tool_output(crow-cli)

add_executable(crow-image-gui WIN32
  src/gui/gui_main.cpp
  src/gui/GuiApp.cpp
  ${CORE_SOURCES}
)
configure_dlss_target(crow-image-gui)
target_include_directories(crow-image-gui PRIVATE src/gui)
set_target_properties(crow-image-gui PROPERTIES OUTPUT_NAME "Crow-DLSS-Rendering-Tool-Image")


add_executable(crow-runtime-selftest
  src/tools/RuntimeSelfTest.cpp
  ${CORE_SOURCES}
)
configure_dlss_target(crow-runtime-selftest)
set_target_properties(crow-runtime-selftest PROPERTIES OUTPUT_NAME "Crow-DLSS-Rendering-Tool-Runtime-Self-Test")
configure_tool_output(crow-runtime-selftest)

add_executable(crow-nvof-selftest
  src/tools/NvofSelfTest.cpp
  src/video/NvofRuntimeProbe.cpp
  src/video/NvofFlowSession.cpp
  src/video/NvofD3D12Bridge.cpp
  src/video/NvofFlowPostprocess.cpp
  src/video/TemporalFlow.cpp
  ${CORE_SOURCES}
)
configure_dlss_target(crow-nvof-selftest)
target_include_directories(crow-nvof-selftest PRIVATE src/video)
set_target_properties(crow-nvof-selftest PROPERTIES OUTPUT_NAME "Crow-DLSS-Rendering-Tool-NVOF-Self-Test")
configure_tool_output(crow-nvof-selftest)

add_executable(crow-nvof-execute-selftest
  src/tools/NvofExecuteSelfTest.cpp
  src/video/NvofRuntimeProbe.cpp
  src/video/NvofFlowSession.cpp
  src/video/NvofD3D12Bridge.cpp
  src/video/NvofFlowPostprocess.cpp
  src/D3D12Context.cpp
)
configure_dlss_target(crow-nvof-execute-selftest)
target_include_directories(crow-nvof-execute-selftest PRIVATE src/video)
set_target_properties(crow-nvof-execute-selftest PROPERTIES OUTPUT_NAME "Crow-DLSS-Rendering-Tool-NVOF-Execute-Self-Test")
configure_tool_output(crow-nvof-execute-selftest)

enable_testing()
add_executable(test-nvof-postprocess EXCLUDE_FROM_ALL
  tests/test_nvof_postprocess.cpp
  src/video/NvofFlowPostprocess.cpp
)
configure_dlss_target(test-nvof-postprocess)
configure_tool_output(test-nvof-postprocess)
add_test(NAME nvof-postprocess COMMAND test-nvof-postprocess)

add_executable(test-temporal-motion-consensus EXCLUDE_FROM_ALL
  tests/test_temporal_motion_consensus.cpp
  src/video/TemporalMotionConsensus.cpp
)
configure_dlss_target(test-temporal-motion-consensus)
configure_tool_output(test-temporal-motion-consensus)
add_test(NAME temporal-motion-consensus COMMAND test-temporal-motion-consensus)

add_executable(test-spatial-motion-consensus EXCLUDE_FROM_ALL
  tests/test_spatial_motion_consensus.cpp
  src/video/SpatialMotionConsensus.cpp
)
configure_dlss_target(test-spatial-motion-consensus)
configure_tool_output(test-spatial-motion-consensus)
add_test(NAME spatial-motion-consensus COMMAND test-spatial-motion-consensus)

add_executable(crow-video-gui WIN32
  src/video/video_main.cpp
  src/video/VideoGuiApp.cpp
  src/fg/DlssFrameGenerationRunner.cpp
  src/fg/NvapiFgPreset.cpp
  ${VIDEO_SOURCES}
  ${CORE_SOURCES}
)
configure_dlss_target(crow-video-gui)
target_include_directories(crow-video-gui PRIVATE src/video src/fg)
if(CROW_HAS_NVAPI)
  target_include_directories(crow-video-gui PRIVATE "${NVAPI_INCLUDE_DIR}")
  target_link_libraries(crow-video-gui PRIVATE "${NVAPI_LIBRARY}")
endif()
set_target_properties(crow-video-gui PROPERTIES OUTPUT_NAME "Crow-DLSS-Rendering-Tool")

# V0.7.2-alpha6: keep the standalone FG application as a diagnostic/regression tool.
# The primary user-facing video application above now owns the unified NR -> FG pipeline.
set(DLSSG_HELPER_HEADER "${NGX_INCLUDE_DIR}/nvsdk_ngx_helpers_dlssg_d3d.h")
if(NOT EXISTS "${DLSSG_HELPER_HEADER}")
  message(FATAL_ERROR
    "DLSS Frame Generation helper header is missing: ${DLSSG_HELPER_HEADER}. "
    "Use a current NVIDIA DLSS SDK checkout/release containing nvsdk_ngx_helpers_dlssg_d3d.h.")
endif()

add_executable(crow-fg-gui WIN32
  src/fg/fg_main.cpp
  src/fg/FgGuiApp.cpp
  src/fg/FgVideoConverter.cpp
  src/fg/DlssFrameGenerationRunner.cpp
  src/video/FfmpegProcess.cpp
  src/video/NvofRuntimeProbe.cpp
  src/video/NvofFlowPostprocess.cpp
  src/video/TemporalMotionConsensus.cpp
  src/video/SpatialMotionConsensus.cpp
  src/video/NvofD3D12Bridge.cpp
  src/video/NvofFlowSession.cpp
  src/video/TemporalFlow.cpp
  src/D3D12Context.cpp
  src/AppPaths.cpp
)
configure_dlss_target(crow-fg-gui)
target_include_directories(crow-fg-gui PRIVATE src/fg src/video)
set_target_properties(crow-fg-gui PROPERTIES OUTPUT_NAME "Crow-DLSS-Rendering-Tool-FG-Diagnostic")
configure_tool_output(crow-fg-gui)

message(STATUS "NGX include: ${NGX_INCLUDE_DIR}")
message(STATUS "NGX library: ${NGX_LIBRARY}")
message(STATUS "TinyEXR: ${TINYEXR_INCLUDE_DIR}")
message(STATUS "NVAPI FG preset support: ${CROW_HAS_NVAPI}")
if(DLSS5_PORTABLE_BUILD)
  message(STATUS "Portable packaging mode: ON (/MD retained for NGX; VC143 CRT will be bundled app-local)")
endif()
message(STATUS "NVOF SDK headers detected: ${DLSS5_HAS_NVOF_SDK}")
message(STATUS "NVOF SDK 5.x D3D12 ABI probe: ${DLSS5_NVOF_D3D12_ABI_OK}")
message(STATUS "NVOF D3D12 execute bridge ready: ${DLSS5_NVOF_D3D12_BRIDGE_READY}")

# Runtime-side Auto Depth helper. The Python environment itself is created on demand
# by setup_auto_depth.ps1; these small bootstrap files are copied next to the GUI.
set(AUTO_DEPTH_DIST "${CMAKE_SOURCE_DIR}/dist/auto_depth")
file(MAKE_DIRECTORY "${AUTO_DEPTH_DIST}")
foreach(_f auto_depth.py auto_depth_video.py dis_flow_video.py requirements.txt setup_auto_depth.ps1)
  configure_file("${CMAKE_SOURCE_DIR}/auto_depth/${_f}" "${AUTO_DEPTH_DIST}/${_f}" COPYONLY)
endforeach()


# Optional SEA-RAFT neural motion backend bootstrap. The official SEA-RAFT checkout,
# PyTorch environment and weights remain runtime-side optional dependencies.
set(SEA_RAFT_DIST "${CMAKE_SOURCE_DIR}/dist/sea_raft")
file(MAKE_DIRECTORY "${SEA_RAFT_DIST}")
foreach(_f sea_raft_video.py spring-S.json spring-M.json requirements.txt setup_sea_raft.ps1 README_zh-CN.md)
  configure_file("${CMAKE_SOURCE_DIR}/sea_raft/${_f}" "${SEA_RAFT_DIST}/${_f}" COPYONLY)
endforeach()

# Runtime-side FFmpeg bootstrap. FFmpeg itself is never bundled in source;
# setup_video.ps1 recognizes a robust local/PATH build and upgrades older local FFmpeg builds when libdav1d is missing.
set(DIAGNOSTIC_DIST "${CMAKE_SOURCE_DIR}/dist/tools")
file(MAKE_DIRECTORY "${DIAGNOSTIC_DIST}")
foreach(_f SELF_TESTS.bat VIDEO_PERFORMANCE_MODE.bat VIDEO_LEGACY_SYNC_SAFE_MODE.bat)
  configure_file("${CMAKE_SOURCE_DIR}/tools/dist/${_f}" "${DIAGNOSTIC_DIST}/${_f}" COPYONLY)
endforeach()

set(VIDEO_DIST "${CMAKE_SOURCE_DIR}/dist/video")
file(MAKE_DIRECTORY "${VIDEO_DIST}")
configure_file("${CMAKE_SOURCE_DIR}/video/setup_video.ps1" "${VIDEO_DIST}/setup_video.ps1" COPYONLY)
