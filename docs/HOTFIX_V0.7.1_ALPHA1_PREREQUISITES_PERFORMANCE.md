# V0.7.1-alpha1 Prerequisite / Performance Hotfix 3

## Build prerequisite changes

- NVOF SDK 5.x is now a mandatory build prerequisite for Full, FG Diagnostic and Portable builds.
- `BUILD.bat` no longer exposes separate NVOF or DLSS-G runtime setup menu actions.
- A valid saved NVOF SDK path is reused automatically; otherwise the build requires the user to select the extracted SDK root. Cancelling aborts the build.
- `build.ps1` and `build_cn.ps1` also reject missing NVOF configuration so lower-level direct builds cannot silently create a nonfunctional NVOF/FG package.

## DLSS-G runtime changes

- `nvngx_dlssg.dll` is mandatory for all unified/FG deliverables.
- CN builds use the acquired Gitee NVIDIA/DLSS mirror checkout; global builds use the acquired official NVIDIA/DLSS checkout.
- If the SDK checkout is absent, runtime setup attempts to acquire NVIDIA/DLSS automatically.
- If an official runtime still cannot be found, the build opens a required file picker for `nvngx_dlssg.dll`.
- Cancelling the picker aborts the build instead of leaving a missing runtime warning.

## Video default execution mode

Direct video conversion now defaults to Performance Mode at program level:

- D3D12 batching enabled;
- CPU worker count automatic;
- inherited legacy-safe overrides are cleared for a normal/direct launch.

`dist/tools/VIDEO_LEGACY_SYNC_SAFE_MODE.bat` explicitly sets `CROW_VIDEO_EXECUTION_MODE=legacy` and remains available only as a diagnostic fallback.
