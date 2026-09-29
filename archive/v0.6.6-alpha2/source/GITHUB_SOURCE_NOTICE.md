# GitHub Source Package

This repository package is the source-only public GitHub edition of
**DLSS5 Filter / Video Converter v0.6.6-alpha2**.

It is based on the current source snapshot:

`Crow-DLSS5-Video-Image-Converter-V0.6.6-alpha2-NVOF-Lifecycle-Tuning-Portable-Generator`

## Intentionally not included

This source package does not contain:

- `nvngx_dlssnr.dll`
- NVIDIA Optical Flow SDK packages or headers copied from a local SDK installation
- `nvofapi64.dll`
- FFmpeg binaries
- downloaded ONNX model weights
- embedded Python runtime / virtual environment
- compiled EXE/DLL/LIB/PDB files
- Portable release output
- build/cache directories
- test video / EXR media
- user settings or logs

The existing build/setup scripts acquire or request required external components
according to the project's normal workflow.

## License

The project source remains licensed under the repository's existing
**GNU General Public License v3 (GPLv3)**. Third-party components retain their
own licenses. See `LICENSE` and `docs/NOTICE.md`.

## Current alpha2 state

The source includes the lifecycle/NVOF tuning hotfix:

- cooperative conversion cancellation;
- ordered NVOF fence/drain/unregister/resource-release/destroy lifecycle;
- explicit NVOF session reset before worker completion;
- NVOF Quality selection;
- NVOF Grid selection with capability validation;
- NVOF Temporal Hints control;
- NVOF Output Cost control;
- Portable Generator source.

Application window titles remain limited to the application name and version.
