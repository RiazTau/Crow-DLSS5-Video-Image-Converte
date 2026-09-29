# GitHub Source Package — v0.7.3-alpha1

This repository is the source-only public GitHub edition of **Crow - DLSS Rendering Tool v0.7.3-alpha1**.

## Release baseline

This public source release is based on the current authoritative Crow development baseline:

`V0.7.3-alpha1-Hotfix1-Strict-Image-Only-SEA-RAFT-Runtime-Hotfix5`

The public release name is shortened to `v0.7.3-alpha1`; the internal lineage is retained in the changelog/hotfix documents.

## Included changes

- Image Conversion `DLSS5 Passes` (`1–8`, default `1`) recursively re-renders each prior DLSS5 result.
- Unified DLSS Neural Rendering, Frame Generation / Multi Frame Generation, NVOF, SEA-RAFT, External Motion / Depth and Auto Depth are presented under the `Crow-DLSS-Rendering-Tool` project name.
- SEA-RAFT Runtime Hotfix5 restores robust venv/PyTorch/torchvision setup, CN/official fallbacks, non-strict Hugging Face loading, and cached-model smoke testing.
- Existing V0.7.2 alpha6/sr2 NVOF, SEA-RAFT, NR, FG/MFG, External Guidance, Auto Depth, Portable and diagnostic paths are retained.
- Public version branding is normalized to `v0.7.3-alpha1`; this packaging change does not alter rendering/build control flow.

## Intentionally not included

This source package does not contain:

- user-supplied `nvngx_dlssnr.dll`;
- NVIDIA Optical Flow SDK packages or copied SDK headers;
- `nvofapi64.dll` from the NVIDIA driver;
- downloaded NVIDIA NVAPI SDK checkout;
- downloaded NVIDIA DLSS/NGX SDK checkout;
- downloaded FFmpeg binaries;
- downloaded ONNX/model payloads;
- CUDA PyTorch or local Python virtual environments;
- upstream SEA-RAFT source checkout or pretrained weights;
- compiled EXE/DLL/LIB/PDB files;
- Portable release output;
- build/cache/log directories;
- user settings, credentials, test videos or EXR media.

## Build entry

Use the root `BUILD.bat`. The build/setup scripts validate or acquire external components according to the selected CN/global workflow. NVIDIA runtime DLLs are staged under `dist/runtime`; diagnostics and self-tests are staged under `dist/tools`.

## License

Crow source remains under GNU GPLv3. Third-party components retain their own licenses. Review `LICENSE` and the expanded `docs/NOTICE.md` dependency/model summary before redistribution, especially when creating a Portable package containing Python wheels, model weights or FFmpeg binaries.
