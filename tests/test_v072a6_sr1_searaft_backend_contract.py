from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8")

def test_searaft_backend_is_selectable_and_wired():
    hdr = read("src/video/VideoConverter.h")
    gui = read("src/video/VideoGuiApp.cpp")
    cpp = read("src/video/VideoConverter.cpp")
    cmake = read("CMakeLists.txt")
    assert "SeaRaft = 5" in hdr
    assert ("SEA-RAFT - Neural HQ (CUDA)" in gui or "SEA-RAFT - Tunable Neural (CUDA)" in gui)
    assert "TemporalMode::SeaRaft" in cpp
    assert "SeaRaftFlowSession.cpp" in cmake

def test_searaft_runtime_adapter_contract():
    cpp = read("src/video/SeaRaftFlowSession.cpp")
    py = read("sea_raft/sea_raft_video.py")
    assert "current -> previous" in py
    assert "model(cur_model, prev_model" in py
    assert "motionUncertainty" in cpp
    assert "sea-raft-video.log" in cpp
    assert "SRD1" in cpp and "SRF1" in cpp

def test_official_dependency_boundary_and_setup():
    setup = read("sea_raft/setup_sea_raft.ps1")
    readme = read("sea_raft/README_zh-CN.md")
    assert "princeton-vl/SEA-RAFT" in setup
    assert "MemorySlices/Tartan-C-T-TSKH-spring540x960-M" in readme
    assert "BSD-3-Clause" in readme
