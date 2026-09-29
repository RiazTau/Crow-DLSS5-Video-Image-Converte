from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read(rel, enc="utf-8-sig"):
    return (ROOT / rel).read_text(encoding=enc)


def test_dlssg_runtime_is_required_and_has_auto_acquire_plus_manual_fallback():
    setup = read("scripts/setup_fg_runtime.ps1")
    assert "https://github.com/NVIDIA/DLSS.git" in setup
    assert "https://gitee.com/mirrors_NVIDIA/DLSS.git" in setup
    assert "clone --depth 1" in setup
    assert "Select-DlssgRuntime" in setup
    assert "REQUIRED: Select nvngx_dlssg.dll" in setup
    assert "Required NVIDIA DLSS Frame Generation runtime was not provided" in setup
    assert "nvngx_dlssg.dll" in setup

    cn = read("scripts/auto_build_cn.ps1")
    gl = read("scripts/auto_build.ps1")
    assert "setup_fg_runtime.ps1') -ChinaMirror -Required" in cn
    assert "setup_fg_runtime.ps1') -Required" in gl
    assert "Required DLSS-G runtime is missing after build" in cn
    assert "Required DLSS-G runtime is missing after build" in gl


def test_nvof_is_mandatory_in_full_fg_and_portable_entry_paths():
    menu = read("BUILD.bat", "ascii")
    assert menu.count("call :REQUIRE_NVOF") >= 6
    assert "setup_nvof_sdk.ps1\" -Required -PreferSaved" in menu
    for rel in (
        "scripts/auto_build.ps1",
        "scripts/auto_build_cn.ps1",
        "scripts/build_fg.ps1",
        "scripts/build_fg_cn.ps1",
        "scripts/portable_build.ps1",
        "scripts/portable_build_cn.ps1",
    ):
        text = read(rel)
        assert "setup_nvof_sdk.ps1" in text
        assert "-Required -PreferSaved" in text


def test_direct_video_conversion_defaults_to_performance_mode():
    converter = read("src/video/VideoConverter.cpp", "utf-8")
    perf = read("tools/dist/VIDEO_PERFORMANCE_MODE.bat", "utf-8")
    legacy = read("tools/dist/VIDEO_LEGACY_SYNC_SAFE_MODE.bat", "utf-8")
    assert "ApplyVideoExecutionModePolicy" in converter
    assert 'CROW_VIDEO_EXECUTION_MODE' in converter
    assert '_putenv_s("DLSS5_DISABLE_D3D12_BATCH", "")' in converter
    assert '_putenv_s("DLSS5_PERF_THREADS", "")' in converter
    assert 'mode == "legacy"' in converter
    assert 'CROW_VIDEO_EXECUTION_MODE=performance' in perf
    assert 'CROW_VIDEO_EXECUTION_MODE=legacy' in legacy
    assert 'Video execution mode: PERFORMANCE by default' in converter
