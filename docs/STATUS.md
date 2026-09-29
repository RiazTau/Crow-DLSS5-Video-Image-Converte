# Current development status — v0.7.3-alpha1

Current public source release: **Crow - DLSS Rendering Tool v0.7.3-alpha1**.

Authoritative development lineage:
`V0.7.3-alpha1-Hotfix1-Strict-Image-Only-SEA-RAFT-Runtime-Hotfix5`.

## v0.7.3-alpha1 additions

- Image Conversion adds `DLSS5 Passes` (`1–8`, default `1`).
- Each completed DLSS5 image result is fed back into the next pass; the existing `Iterations` parameter remains independent.
- SEA-RAFT Runtime Hotfix5 restores the validated runtime setup/model-loading path without modifying Crow's Build Center, CMake architecture, NVOF logic, video/FG C++ pipeline, or the C++ SEA-RAFT bridge.

## SEA-RAFT Runtime Hotfix5

- `$Args` PowerShell automatic-variable collision removed.
- Venv creation explicitly validates `.venv\Scripts\python.exe`.
- CUDA `torch` and `torchvision` are independently checked/repaired.
- CN mirror-first plus official fallback behavior is retained where supported.
- SEA-RAFT checkout uses GitCode mirror-first in CN mode with official GitHub fallback.
- Model prefetch uses `hf-mirror` first in CN mode with official Hugging Face fallback.
- Hugging Face checkpoints load through `RAFT.from_pretrained(..., strict=False)`.
- `--check-only` validates cached Spring-S / Spring-M model loading.

## Existing alpha6/sr2 pipeline retained

- Predictive Multi-Scale NVOF reconstruction and uncertainty/visibility state.
- NR-safe and FG motion consumer paths.
- SEA-RAFT tunable neural optical flow backend.
- External EXR Motion / Depth.
- Auto Depth.
- FG/MFG and optional NVAPI preset integration.
- Portable and diagnostic build paths.

## Validation for this source package

- Python/contract suite: **48 passed**.
- `sea_raft_video.py` syntax compile: PASS.
- Source archive integrity: PASS.
- This remains alpha software; Windows/RTX runtime behavior depends on external NVIDIA runtimes, SDKs, drivers and hardware.
