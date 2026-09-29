from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8-sig")

def test_primary_exe_is_unified_rendering_tool():
    cmake = read("CMakeLists.txt")
    assert 'OUTPUT_NAME "Crow-DLSS-Rendering-Tool"' in cmake
    target = cmake.split('add_executable(crow-video-gui WIN32',1)[1].split(')',1)[0]
    assert 'src/fg/DlssFrameGenerationRunner.cpp' in target
    assert 'src/video/VideoGuiApp.cpp' in target

def test_settings_expose_nr_and_fg_switches():
    h = read("src/video/VideoConverter.h")
    assert 'bool enableDlssNr = true;' in h
    assert 'bool enableFrameGeneration2X = false;' in h
    assert 'enum class OutputFrameKind' in h
    assert 'VideoPreviewStats' in h

def test_combined_mode_uses_shared_ngx_core_lifetime():
    cpp = read("src/video/VideoConverter.cpp")
    nr_h = read("src/DlssNrRunner.h")
    fg_h = read("src/fg/DlssFrameGenerationRunner.h")
    shared = read("src/NgxCoreSession.cpp")
    assert 'const bool sharedCore = settings.enableDlssNr && settings.enableFrameGeneration2X;' in cpp
    assert 'std::make_unique<NgxCoreSession>' in cpp
    assert 'manageCoreLifetime = true' in nr_h
    assert 'manageCoreLifetime = true' in fg_h
    assert 'Shared NVSDK_NGX_D3D12_Init_with_ProjectID' in shared

def test_pipeline_order_is_nr_then_fg_then_encoder():
    cpp = read("src/video/VideoConverter.cpp")
    nr = cpp.index('output = runner->ProcessTemporal')
    fg = cpp.index('fgRunner->GenerateFrame')
    publish = cpp.index('outputOk = publishOutput(*generated')
    assert nr < fg < publish
    assert 'const uint32_t fgMultiplier = EffectiveFgMultiplier(settings);' in cpp
    assert 'info.fps * static_cast<double>(fgMultiplier)' in cpp
    assert 'const uint32_t generatedCount = fgMultiplier - 1u;' in cpp

def test_preview_consumes_final_output_sequence_and_reports_fps_kind():
    cpp = read("src/video/VideoConverter.cpp")
    gui = read("src/video/VideoGuiApp.cpp")
    assert 'publishOutput' in cpp
    assert 'callbacks.onPreview(input, encodedFrame' in cpp
    assert 'OutputFrameKind::Generated' in cpp
    assert 'OutputFrameKind::Fallback' in cpp
    assert 'FINAL OUTPUT - LIVE' in gui
    assert 'Source %.3f FPS   Output %.3f FPS   Preview %.1f FPS' in gui
    assert 'Frame %llu/%llu' in gui
    assert 'L"FG"' in gui and 'L"REAL"' in gui and 'L"FALLBACK"' in gui

def test_fg_ui_allows_only_validated_motion_guidance_paths():
    gui = read("src/video/VideoGuiApp.cpp")
    conv = read("src/video/VideoConverter.cpp")
    assert 'video::TemporalMode::ExternalExr' in gui
    assert 'video::TemporalMode::NvidiaOpticalFlow' in gui
    assert ('requires NVIDIA Optical Flow or External EXR Motion guidance' in conv or 'requires NVIDIA Optical Flow, SEA-RAFT Neural Motion, or External EXR Motion guidance' in conv)

def test_standalone_fg_moved_to_tools_as_diagnostic():
    cmake = read("CMakeLists.txt")
    assert 'OUTPUT_NAME "Crow-DLSS-Rendering-Tool-FG-Diagnostic"' in cmake
    assert 'configure_tool_output(crow-fg-gui)' in cmake
    fg = read("src/fg/FgVideoConverter.cpp")
    assert 'app::DistributionRoot() / L"runtime"' in fg

def test_portable_requires_both_ngx_runtimes():
    ps = read("scripts/make_portable.ps1")
    manifest = read("scripts/portable_stage.py")
    assert 'runtime\\nvngx_dlssnr.dll' in ps
    assert 'runtime\\nvngx_dlssg.dll' in ps
    assert 'runtime/nvngx_dlssnr.dll' in manifest
    assert 'runtime/nvngx_dlssg.dll' in manifest
    assert 'official NVIDIA DLSS Frame Generation runtime nvngx_dlssg.dll' in ps
    assert 'tools\\Crow-DLSS-Rendering-Tool-NVOF-Self-Test.exe' in ps
    assert 'tools\\Crow-DLSS-Rendering-Tool-NVOF-Execute-Self-Test.exe' in ps


def test_tool_scripts_use_v071_names_and_layout():
    self_tests = read("tools/dist/SELF_TESTS.bat")
    perf = read("tools/dist/VIDEO_PERFORMANCE_MODE.bat")
    legacy = read("tools/dist/VIDEO_LEGACY_SYNC_SAFE_MODE.bat")
    assert "Crow-DLSS-Rendering-Tool-Runtime-Self-Test.exe" in self_tests
    assert "Crow-DLSS-Rendering-Tool-NVOF-Self-Test.exe" in self_tests
    assert "Crow-DLSS-Rendering-Tool-NVOF-Execute-Self-Test.exe" in self_tests
    assert "..\\Crow-DLSS-Rendering-Tool.exe" in perf
    assert "..\\Crow-DLSS-Rendering-Tool.exe" in legacy
    assert "Crow-DLSS5-Video-Image-Converter" not in self_tests + perf + legacy
