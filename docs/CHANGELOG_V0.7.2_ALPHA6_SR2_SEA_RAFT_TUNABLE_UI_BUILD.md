# V0.7.2-alpha6-sr2 — SEA-RAFT Tunable Neural Motion

- Full/Portable Build automatically installs SEA-RAFT CUDA runtime.
- CUDA PyTorch installation is isolated from ordinary requirements; prevents CPU-wheel overwrite.
- CN setup supports GitCode / hf-mirror / Alibaba PyPI with official fallback.
- SEA-RAFT UI: Spring-S/M, quarter/half/full inference resolution, 1-12 refinements, Neural Flow Trust, Uncertainty Sensitivity.
- Two-line parameter labels receive a 38-DPI-scaled height and parameter rows use expanded spacing to prevent clipping.
- WM_SIZE / WM_DPICHANGED explicitly invalidates and redraws the full preview surfaces to remove maximize/full-screen resize ghosts.
- Removed pause / Press-any-key blocking behavior from shipped batch scripts.
- DLSSNR picker is named generically; RTX 40-series receives a special-version notice instead of a misleading "40-series compatible" label.
