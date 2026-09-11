from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]

def text(rel):
    return (ROOT / rel).read_text(encoding="utf-8-sig")

def main():
    cmake=text("CMakeLists.txt")
    assert "option(DLSS5_PORTABLE_BUILD" in cmake
    assert 'MSVC_RUNTIME_LIBRARY "MultiThreadedDLL"' in cmake
    assert "Do NOT switch this target to /MT" in cmake
    assert (ROOT/"BUILD_PORTABLE_CN.bat").exists()
    assert (ROOT/"BUILD_PORTABLE.bat").exists()
    make=text("scripts/make_portable.ps1")
    assert "Microsoft.VC143.CRT" in make
    assert "portable-builder-python.path" in make
    assert "python-$pyVersion-embed-amd64.zip" in make
    assert "Copy the same app-local VC runtime here as well" in make
    assert "Join-Path $pyScripts $dll.Name" in make
    assert "onnxruntime" in text("scripts/portable_stage.py")
    assert "onnxruntime\") / \"tools\"" in text("scripts/portable_stage.py")
    zip_py=text("scripts/zip_portable.py")
    assert "allowZip64=True" in zip_py
    assert "Compress-Archive" in zip_py
    assert "testzip()" in zip_py
    cn_portable=text("scripts/portable_build_cn.ps1")
    assert "NVOF_SDK_SETUP.bat" in cn_portable
    assert "scripts\\import_runtime.ps1" in cn_portable
    assert "never auto-downloaded" in text("BUILD_PORTABLE_CN.bat")
    gui=text("src/gui/GuiApp.cpp")
    vgui=text("src/video/VideoGuiApp.cpp")
    assert 'L"Crow-DLSS5-Video-Image-Converter V0.6.6-alpha2"' in gui
    assert 'L"Crow-DLSS5-Video-Image-Converter V0.6.6-alpha2"' in vgui
    print("PASS: V0.6.6-alpha2 portable generator contract")

if __name__ == "__main__":
    main()
