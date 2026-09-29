from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
menu = (ROOT / "BUILD.bat").read_text(encoding="ascii")
nvof = (ROOT / "scripts" / "setup_nvof_sdk.ps1").read_text(encoding="utf-8-sig")

# NVOF/FG runtime setup is not exposed as a separate menu action.
# [7] is intentionally reserved for the explicit NVAPI maintenance refresh.
assert '[7] Re-download / refresh NVIDIA NVAPI SDK' in menu
assert 'goto NVAPI_REFRESH' in menu
assert '[8]' not in menu
assert 'goto NVOF' not in menu
assert 'goto FG_RUNTIME' not in menu

# Every user-facing build path must pass the mandatory NVOF prerequisite gate.
for label in (':FULL_CN', ':FULL_GLOBAL', ':FG_CN', ':FG_GLOBAL', ':PORTABLE_CN', ':PORTABLE_GLOBAL'):
    block = menu.split(label, 1)[1]
    block = block.split('\n:', 1)[0]
    assert 'call :REQUIRE_NVOF' in block

assert ':REQUIRE_NVOF' in menu
assert 'setup_nvof_sdk.ps1" -Required -PreferSaved' in menu
assert 'NVOF SDK validation/selection is now a mandatory build step.' in menu
assert '[switch]$Required' in nvof
assert '[switch]$PreferSaved' in nvof
assert 'Build cancelled' in nvof
print('PASS: Build Center makes NVOF a mandatory integrated prerequisite')
