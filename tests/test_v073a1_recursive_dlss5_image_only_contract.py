from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
src = (ROOT / "src/gui/GuiApp.cpp").read_text(encoding="utf-8-sig")

def test_recursive_pass_ui_and_loop():
    assert 'L"DLSS5 Passes"' in src
    assert 'recursive_dlss5_pass_count' in src
    assert 'static constexpr uint32_t kPassCounts[] = {1, 2, 3, 4, 5, 6, 7, 8};' in src
    assert 'for (uint32_t passIndex = 0; passIndex < dlssOverlayPassCount; ++passIndex)' in src
    assert 'result = runner.Process(result, depth ? &depth->values : nullptr);' in src
