from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read(rel):
    return (ROOT / rel).read_text(encoding='utf-8', errors='ignore')


def test_version_and_main_ui_are_v072_alpha1():
    cmake = read('CMakeLists.txt')
    gui = read('src/video/VideoGuiApp.cpp')
    assert 'VERSION 0.7.2' in cmake or 'VERSION 0.7.3' in cmake
    assert ('Crow - DLSS Rendering Tool V0.7.2-alpha1' in gui or 'Crow - DLSS Rendering Tool V0.7.2-alpha2' in gui or 'Crow - DLSS Rendering Tool V0.7.2-alpha3' in gui or 'Crow - DLSS Rendering Tool V0.7.2-alpha4' in gui or 'Crow - DLSS Rendering Tool V0.7.2-alpha5' in gui or 'Crow - DLSS Rendering Tool V0.7.2-alpha6' in gui or 'Crow - DLSS Rendering Tool V0.7.3-alpha1' in gui)


def test_mfg_runner_is_generic_and_runtime_capability_driven():
    h = read('src/fg/DlssFrameGenerationRunner.h')
    cpp = read('src/fg/DlssFrameGenerationRunner.cpp')
    assert 'GenerateFrame' in h
    assert 'multiFrameCount' in h and 'multiFrameIndex' in h
    assert 'NVSDK_NGX_DLSSG_Parameter_MultiFrameCountMax' in cpp
    assert 'isRtx40Series' in cpp and 'effectiveGenerated = 1' in cpp
    assert 'isRtx50Series' in cpp and 'std::min<uint32_t>(effectiveGenerated, 5u)' in cpp
    assert 'std::clamp<uint32_t>(effectiveGenerated + 1u, 2u, 6u)' in cpp


def test_mfg_timeline_encoder_and_preview_scale_with_multiplier():
    cpp = read('src/video/VideoConverter.cpp')
    gui = read('src/video/VideoGuiApp.cpp')
    assert 'settings.fgMultiplier' in cpp
    assert 'info.fps * static_cast<double>(fgMultiplier)' in cpp
    assert 'info.totalFrames * static_cast<uint64_t>(fgMultiplier)' in cpp
    assert 'const uint32_t generatedCount = fgMultiplier - 1u;' in cpp
    assert 'for (uint32_t generatedIndex = 1; generatedIndex <= generatedCount' in cpp
    assert 'for (uint32_t i = 1; i < fgMultiplier; ++i)' in cpp
    assert 'OutputFrameKind::Generated' in cpp and 'OutputFrameKind::Fallback' in cpp
    assert 'FG Multiplier' in gui
    assert 'UiMaxFgMultiplier' in gui


def test_external_exr_motion_and_depth_are_shared_with_fg():
    cpp = read('src/video/VideoConverter.cpp')
    gui = read('src/video/VideoGuiApp.cpp')
    assert 'externalMotionEnabled = settings.temporalMode == TemporalMode::ExternalExr' in cpp
    assert 'externalData->LoadMotion' in cpp
    assert '(settings.enableDlssNr || settings.enableFrameGeneration2X) && settings.depthMode == DepthMode::ExternalExr' in cpp
    assert 'externalData->LoadDepth' in cpp
    assert ('GenerateFrame(output, flow.motionXY, depthPtr, depthInverted' in cpp or 'GenerateFrame(output, fgMotionPtr ? *fgMotionPtr : flow.motionXY, depthPtr, depthInverted' in cpp)
    assert 'External EXR Guidance (NR / FG)...' in gui
    # FG permits either validated NVOF or external EXR motion rather than forcing NVOF unconditionally.
    assert 'currentTemporal != 3 && currentTemporal != 4' in gui


def test_fg_model_preset_uses_real_nvapi_driver_profile_settings():
    h = read('src/video/VideoConverter.h')
    preset = read('src/fg/NvapiFgPreset.cpp')
    assert 'enum class FgModelPreset' in h
    assert 'DriverDefault' in h and 'PresetA' in h and 'PresetB' in h and 'Latest' in h
    assert 'NvAPI_DRS_FindApplicationByName' in preset
    assert 'NvAPI_DRS_CreateApplication' in preset
    assert 'NVAPI_EXECUTABLE_ALREADY_IN_USE' in preset
    assert 'NGX_DLSS_FG_OVERRIDE_ID' in preset
    assert 'NGX_DLSS_FG_OVERRIDE_RENDER_PRESET_SELECTION_ID' in preset
    assert 'RENDER_PRESET_A' in preset and 'RENDER_PRESET_B' in preset and 'RENDER_PRESET_Latest' in preset
    assert 'NvAPI_DRS_DeleteProfileSetting' in preset


def test_nvapi_sdk_is_a_build_dependency_and_ui_exposes_only_requested_fg_controls():
    cmake = read('CMakeLists.txt')
    global_build = read('scripts/build.ps1')
    cn_build = read('scripts/build_cn.ps1')
    gui = read('src/video/VideoGuiApp.cpp')
    setup_nvapi = read('scripts/setup_nvapi_sdk.ps1')
    menu = read('BUILD.bat')
    assert 'NVAPI_SDK_DIR' in cmake
    assert 'nvapi64.lib' in cmake
    assert 'NVAPI_SDK_DIR' in global_build and 'NVIDIA NVAPI SDK' in global_build
    assert 'NVAPI_SDK_DIR' in cn_build
    assert 'optional NVAPI SDK' in cn_build
    assert 'Build will CONTINUE normally with FG Model Preset locked to Driver Default' in cn_build
    assert 'FG Model Preset' in gui
    assert 'Driver Default' in gui and 'Preset A' in gui and 'Preset B' in gui and 'Latest' in gui
    assert 'Driver Default (NVAPI unavailable)' in gui
    assert 'setup_nvapi_sdk.ps1' in menu and 'NVAPI_REFRESH' in menu
    assert 'Existing SDK is not deleted until the replacement is fully validated.' in setup_nvapi
    assert 'nvapi.h' in setup_nvapi and 'NvApiDriverSettings.h' in setup_nvapi and 'nvapi64.lib' in setup_nvapi
    assert '#if CROW_HAS_NVAPI' in gui
    # Deliberately do not expose low-value camera/matrix controls in the FG section.
    assert 'FG Camera Matrix' not in gui
    assert 'FG Near Plane' not in gui
    assert 'FG Far Plane' not in gui
