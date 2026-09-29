# Upgrade from V0.3.2 to V0.4.0

1. Overlay the V0.4 upgrade package on the V0.3.2 project root.
2. Keep your existing `.deps` folder; NVIDIA DLSS SDK and TinyEXR do not need to be downloaded again.
3. Keep `dist/runtime/nvngx_dlssnr.dll` if it already exists.
4. Rebuild:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\build.ps1 -Clean
```

Expected executables:

```text
dist\Crow-DLSS5-Video-Image-Converter-CLI.exe
dist\Crow-DLSS5-Video-Image-Converter-Image.exe
dist\Crow-DLSS5-Video-Image-Converter-Video.exe
```

## FFmpeg

Manual layout supported by V0.4:

```text
dist\video\ffmpeg\bin\ffmpeg.exe
dist\video\ffmpeg\bin\ffprobe.exe
```

If those files are already present, `Setup FFmpeg` does not download them again.

Alternatively place the official Gyan ZIP here before clicking Setup:

```text
dist\video\ffmpeg-release-essentials.zip
```

The setup script will extract it locally.

## Auto Depth

If the V0.3 Auto Depth environment is already installed, it is reused. V0.4 adds `auto_depth_video.py` beside the existing helper; the same venv and model are used.
