# V0.6.6-alpha2 Portable Generator Integration

## Scope

Carries the validated V0.6.5.3 Portable Edition packaging architecture into the current V0.6.6-alpha2 NVOF branch without reverting NVOF lifecycle/tuning changes.

## Important CRT rule

The final portable design **does not force /MT**. NVIDIA `nvsdk_ngx_d.lib` is built with the dynamic MSVC runtime and the V0.6.5.3 /MT experiment produced LNK2038/LNK2005 conflicts. Portable mode therefore keeps `/MD` and copies the latest x64 `Microsoft.VC143.CRT` DLL set app-local beside the converter executables and beside the embedded Python executable, so native Python extensions also work on a clean target PC.

## Portable output

`BUILD_PORTABLE_CN.bat` (recommended for mainland China) or `BUILD_PORTABLE.bat` produces:

```text
portable/
  Crow-DLSS5-Video-Image-Converter-V0.6.6-alpha2-Portable-x64/
  Crow-DLSS5-Video-Image-Converter-V0.6.6-alpha2-Portable-x64.zip
```

The tree contains the converter EXEs, manually imported DLSSNR runtime, app-local VC143 CRT, FFmpeg, Depth Anything V2 model, and relocatable embedded CPython with AutoDepth/DIS packages.

The NVIDIA Optical Flow SDK is never redistributed. `nvofapi64.dll` remains supplied by the NVIDIA display driver on the target PC.

## Long-path protection

- `scripts/portable_stage.py` removes stale portable trees and prunes unused `onnxruntime/tools` plus Python caches using Python long-path support.
- `scripts/zip_portable.py` replaces Windows PowerShell 5.1 `Compress-Archive`, uses Zip64, preserves the top-level portable folder, skips caches, and verifies the generated ZIP.
- This specifically prevents recurrence of the V0.6.5.3 deep ONNX Runtime path failure.

## Runtime import policy

Portable generation never downloads `nvngx_dlssnr.dll`. If it is not already present at `dist/runtime/nvngx_dlssnr.dll`, the portable launcher opens the existing manual runtime import picker before compilation. The NVOF SDK must also be configured first with `NVOF_SDK_SETUP.bat`; it is used only at compile time and is never copied to the portable tree.

## Window title rule

Main GUI window titles are now limited to product name + version only.
