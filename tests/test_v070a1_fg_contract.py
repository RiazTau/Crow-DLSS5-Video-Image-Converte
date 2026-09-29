from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8-sig")


def test_standalone_target_and_name():
    cmake = read("CMakeLists.txt")
    assert "add_executable(crow-fg-gui WIN32" in cmake
    assert ('OUTPUT_NAME "Crow-DLSS-Rendering-Tool-FG"' in cmake or 'OUTPUT_NAME "Crow-DLSS-Rendering-Tool-FG-Diagnostic"' in cmake)
    assert "src/fg/FgGuiApp.cpp" in cmake


def test_direct_ngx_dlssg_output_contract():
    src = read("src/fg/DlssFrameGenerationRunner.cpp")
    assert "NGX_D3D12_CREATE_DLSSG" in src
    assert "NGX_D3D12_EVALUATE_DLSSG" in src
    assert "eval.pOutputInterpFrame" in src
    assert "eval.pOutputDisableInterpolation" in src
    assert "opt.multiFrameCount = multiFrameCount" in src
    assert "opt.multiFrameIndex = multiFrameIndex" in src
    assert "DXGI_FORMAT_R16G16_FLOAT" in src
    assert "DXGI_FORMAT_R32_FLOAT" in src
    assert "NVSDK_NGX_DLSSG_Parameter_BackbufferFrameID" in src


def test_video_pipeline_uses_nvof_and_exact_2x_timeline():
    src = read("src/fg/FgVideoConverter.cpp")
    assert "video::NvofFlowSession" in src
    assert "fgRunner.GenerateFrame" in src
    assert ", 1, 1)" in src
    assert "info.fps * 2.0" in src
    assert "2*N output frames at 2*FPS" in src
    assert "encoder.WriteExact(previous.pixels.data()" in src


def test_cancel_close_is_lifecycle_safe():
    src = read("src/fg/FgGuiApp.cpp")
    assert "pendingClose = true" in src
    assert "cancelRequested.store(true" in src
    assert "WM_FG_DONE" in src
    assert "worker.join()" in src


def test_runtime_staging_is_official_sdk_relative():
    src = read("scripts/setup_fg_runtime.ps1")
    assert "NVIDIA-DLSS" in src
    assert "lib\\Windows_x86_64\\rel\\nvngx_dlssg.dll" in src
    assert "Get-AuthenticodeSignature" in src


def test_fg_build_wrapper_uses_named_hashtable_splatting():
    src = read("scripts/build_fg.ps1")
    assert "$buildParams = @{" in src
    assert "@buildParams" in src
    assert "$runtimeParams = @{}" in src
    assert "@runtimeParams" in src
    # Regression: ordinary array splatting caused Release to bind to NgxSdkDir.
    assert "$base = @(" not in src
    assert "@base" not in src
    assert "$runtimeArgs = @(" not in src
    assert "@runtimeArgs" not in src
