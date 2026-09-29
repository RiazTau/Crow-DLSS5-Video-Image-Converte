from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sea = (ROOT / "src/video/SeaRaftFlowSession.cpp").read_text(encoding="utf-8-sig")
build = (ROOT / "BUILD.bat").read_text(encoding="utf-8-sig")
opener = (ROOT / "scripts/open_latest_build_log.ps1").read_text(encoding="utf-8-sig")

# Windows SDK headers expose a legacy `small` token; do not use it as a local identifier.
assert "const bool small" not in sea
assert "const bool useSmallModel" in sea
for token in [
    'useSmallModel ? L"spring-S.json" : L"spring-M.json"',
    'useSmallModel ? L"sea-raft-spring-S.pth" : L"sea-raft-spring-M.pth"',
    'useSmallModel ? L"spring-S" : L"spring-M"',
    'useSmallModel ? L"MemorySlices/Tartan-C-T-TSKH-spring540x960-S"',
]:
    assert token in sea

# Build failures must remain non-blocking but surface the newest diagnostic log.
assert "open_latest_build_log.ps1" in build
assert "CROW_BUILD_NO_OPEN_LOG" in build
assert "pause" not in build.lower()
assert "Start-Process" in opener and "notepad.exe" in opener
assert "cmake-build*.log" in opener and "auto-build*.log" in opener

print("PASS: sr2 MSVC SEA-RAFT compile hotfix1 contract")
