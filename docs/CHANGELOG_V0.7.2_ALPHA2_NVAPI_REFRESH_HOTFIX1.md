# Crow - DLSS Rendering Tool V0.7.2-alpha2 — NVAPI Refresh Hotfix 1

This maintenance hotfix adds an explicit NVIDIA NVAPI SDK refresh action to the unified Build Center.

## Build Center

`BUILD.bat` now exposes:

`[7] Re-download / refresh NVIDIA NVAPI SDK (FG Presets)`

The action runs `scripts/setup_nvapi_sdk.ps1 -ForceRedownload`.

## Transactional refresh behavior

The refresh script:

1. downloads into a temporary staging directory;
2. tries the official NVIDIA `nvapi` git repository first;
3. falls back to the official NVIDIA GitHub source archive;
4. validates `nvapi.h`, `NvApiDriverSettings.h`, and `amd64\\nvapi64.lib`;
5. replaces `.deps\\NVIDIA-nvapi` only after validation succeeds;
6. restores/preserves the previous SDK if replacement fails.

A failed NVAPI refresh does not affect NR, FG/MFG, NVOF, External EXR Guidance, or Driver Default FG behavior. Preset A/B/Latest still require a build made with a valid NVAPI SDK.
