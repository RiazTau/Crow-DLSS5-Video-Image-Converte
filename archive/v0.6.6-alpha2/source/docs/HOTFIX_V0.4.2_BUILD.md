# V0.4.2 Build Hotfix

Fixes the first MSVC compile failure in V0.4.1 video GUI:

- `VideoGuiApp.cpp(338): error C2466: cannot allocate an array of constant size 0`
- Removed an unused zero-length native C++ array left in the layout function.
- No DLSSNR, FFmpeg, Auto Depth, image GUI, or runtime behavior was changed.

Apply by replacing:

`src/video/VideoGuiApp.cpp`

Then rebuild:

```powershell
.\scripts\build.ps1 -Clean
```
