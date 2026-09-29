from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def read(p): return (ROOT/p).read_text(encoding="utf-8-sig")

def test_searaft_tuning_reaches_worker():
    gui=read("src/video/VideoGuiApp.cpp"); conv=read("src/video/VideoConverter.cpp"); worker=read("sea_raft/sea_raft_video.py")
    assert "SEA-RAFT - Tunable Neural (CUDA)" in gui
    for token in ["IDC_SEARAFT_MODEL","IDC_SEARAFT_SCALE","IDC_SEARAFT_ITERS","IDC_SEARAFT_TRUST","IDC_SEARAFT_UNCERTAINTY"]: assert token in gui
    assert "settings.seaRaft" in conv
    assert "--scale" in worker and "--iters" in worker and "--uncertainty-sensitivity" in worker

def test_build_installs_cuda_searaft_without_cpu_torch_requirements():
    req=read("sea_raft/requirements.txt"); setup=read("sea_raft/setup_sea_raft.ps1")
    assert "\ntorch\n" not in "\n"+req+"\n"
    assert "download.pytorch.org/whl/cu130" in setup
    assert "torch.cuda.is_available" in setup
    assert "-ChinaMirror" in read("scripts/auto_build_cn.ps1")
    assert "dist\\sea_raft\\setup_sea_raft.ps1" in read("scripts/auto_build.ps1")

def test_ui_resize_redraw_and_labels():
    gui=read("src/video/VideoGuiApp.cpp")
    assert "labelH=DpiScale(s,38)" in gui
    assert "RefreshPreviewSurfaces(s)" in gui
    assert "RDW_INVALIDATE|RDW_FRAME|RDW_ALLCHILDREN|RDW_UPDATENOW" in gui

def test_no_pause_in_shipped_batch_files():
    for rel in ["BUILD.bat","tools/dist/SELF_TESTS.bat","tools/dist/VIDEO_LEGACY_SYNC_SAFE_MODE.bat","tools/dist/VIDEO_PERFORMANCE_MODE.bat"]:
        assert "pause" not in read(rel).lower()
