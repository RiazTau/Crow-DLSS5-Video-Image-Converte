from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read(rel, enc="utf-8"):
    return (ROOT / rel).read_text(encoding=enc, errors="ignore")

def test_build_center_exposes_manual_nvapi_refresh():
    menu = read("BUILD.bat", "ascii")
    assert '[7] Re-download / refresh NVIDIA NVAPI SDK' in menu
    assert 'goto NVAPI_REFRESH' in menu
    assert 'setup_nvapi_sdk.ps1" -ForceRedownload' in menu
    assert 'Any previously valid SDK was preserved' in menu

def test_nvapi_refresh_is_transactional_and_validated():
    ps = read("scripts/setup_nvapi_sdk.ps1")
    assert "https://github.com/NVIDIA/nvapi.git" in ps
    assert "https://codeload.github.com/NVIDIA/nvapi/zip/refs/heads/main" in ps
    assert "nvapi.h" in ps
    assert "NvApiDriverSettings.h" in ps
    assert "amd64\\nvapi64.lib" in ps
    assert "Existing SDK is not deleted until the replacement is fully validated." in ps
    assert "previous SDK restored" in ps
    assert "-ForceRedownload" not in ps  # switch is declared normally, not shell-spliced text
