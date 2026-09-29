from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def test_single_root_build_entrypoint():
    bats = sorted(p.name for p in ROOT.glob("*.bat"))
    assert bats == ["BUILD.bat"]
    menu = (ROOT / "BUILD.bat").read_text(encoding="ascii")
    for token in ["full-cn", "full", "fg-cn", "fg", "portable-cn", "portable", "setup_nvof_sdk.ps1"]:
        assert token in menu
    # DLSS-G runtime setup is mandatory inside the concrete build workflows rather
    # than exposed as a separate root-menu action.
    for rel in ["scripts/auto_build.ps1", "scripts/auto_build_cn.ps1", "scripts/build_fg.ps1", "scripts/build_fg_cn.ps1", "scripts/portable_build.ps1", "scripts/portable_build_cn.ps1"]:
        assert "setup_fg_runtime.ps1" in (ROOT / rel).read_text(encoding="utf-8-sig")

def test_tool_outputs_are_not_in_dist_root():
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8-sig")
    assert 'RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/dist/tools"' in cmake
    for target in ["crow-cli", "crow-runtime-selftest", "crow-nvof-selftest", "crow-nvof-execute-selftest", "test-nvof-postprocess"]:
        assert f"configure_tool_output({target})" in cmake

def test_diagnostic_scripts_are_staged_under_tools():
    for name in ["SELF_TESTS.bat", "VIDEO_PERFORMANCE_MODE.bat", "VIDEO_LEGACY_SYNC_SAFE_MODE.bat"]:
        assert (ROOT / "tools" / "dist" / name).exists()
    finalizer = (ROOT / "scripts" / "finalize_dist.ps1").read_text(encoding="utf-8-sig")
    assert "No loose command scripts are allowed at the dist root" in finalizer

def test_tools_resolve_runtime_from_distribution_root():
    h = (ROOT / "src" / "AppPaths.h").read_text(encoding="utf-8-sig")
    cpp = (ROOT / "src" / "AppPaths.cpp").read_text(encoding="utf-8-sig")
    assert "DistributionRoot" in h
    assert 'dir.filename() == L"tools"' in cpp
    assert 'DistributionRoot() / L"runtime"' in cpp
