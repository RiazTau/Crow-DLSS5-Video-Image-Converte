# V0.4.0 first target-machine test

## 1. Build

```powershell
cd C:\path\to\Crow-DLSS5-Video-Image-Converter-V0.4.0
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\build.ps1 -Clean
```

Expected:

```text
dist\Crow-DLSS5-Video-Image-Converter-CLI.exe
dist\Crow-DLSS5-Video-Image-Converter-Image.exe
dist\Crow-DLSS5-Video-Image-Converter-Video.exe
```

## 2. Runtime

Make sure this exists:

```text
dist\runtime\nvngx_dlssnr.dll
```

The video GUI also has a `Runtime DLL...` button.

## 3. FFmpeg

Preferred manual layout:

```text
dist\video\ffmpeg\bin\ffmpeg.exe
dist\video\ffmpeg\bin\ffprobe.exe
```

Or click `Setup FFmpeg`.

## 4. First video test

Use a short 5-10 second SDR MP4 first.

Recommended first settings:

```text
Depth Mode: Zero Depth
NR Preset: Preset #2
NR Style: Cinematic
Intensity: 0.85
Local Tone: 1.0
Local Structure: 1.0
Skin Structure: -0.5
Iterations: 1
Encoder: H.264 NVENC
CQ: 18
```

Check that:

- ffprobe metadata appears after choosing the input.
- the progress bar advances.
- Original and DLSS5 live previews update during conversion.
- the completed MP4 plays normally and contains audio when the source contains audio.

## 5. Auto Depth test

Click `Setup Auto Depth`, then choose:

```text
Depth Mode: Auto Depth - Depth Anything V2
DAV2 Size: 518
```

During conversion the third live preview should update with the actual depth guidance sent to DLSSNR.

## 6. Logs

When FFmpeg fails, inspect:

```text
dist\video\logs\decoder-last.log
dist\video\logs\encoder-last.log
```

Auto Depth video log:

```text
dist\video\auto-depth-video.log
```

For the first Windows/MSVC failure, report the first real `error Cxxxx`, `LNKxxxx`, or runtime error dialog rather than third-party warnings above it.
