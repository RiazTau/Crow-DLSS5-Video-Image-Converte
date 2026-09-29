# V0.4.4 FFmpeg compatibility hotfix

Fixes video decode startup failure such as:

```
FFmpeg decoder failed.
Unrecognized option 'vsync'.
Error splitting the argument list: Option not found
```

## Cause
V0.4.1-V0.4.3 unconditionally passed `-vsync 0` to the decoder. Some newer
FFmpeg builds no longer expose that deprecated option.

## Fix
`VideoConverter.cpp` now inspects `ffmpeg -h full` once when a conversion
starts and selects the best supported mode:

1. `-fps_mode passthrough` when available (preferred/current FFmpeg syntax)
2. `-vsync 0` for compatible older builds
3. no explicit sync option if neither is advertised

The capability probe has a 5-second bound and failure is non-fatal.

## Apply over V0.4.1 / V0.4.2 / V0.4.3
Replace:

```
src/video/VideoConverter.cpp
```

then rebuild:

```powershell
.\scripts\build.ps1 -Clean
```
