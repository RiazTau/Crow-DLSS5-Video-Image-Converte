# V0.7.2-alpha6-sr2 — SEA-RAFT Tunable Neural Motion

## Scope

This branch keeps V0.7.2-alpha6-sr1 as its functional base and focuses on productionizing the SEA-RAFT path after RTX testing showed a substantially larger visual-quality gain than continued NVOF heuristic reconstruction. NVOF remains available for fast hardware optical flow and A/B comparison.

## SEA-RAFT runtime deployment

- Full and Portable Build now install SEA-RAFT automatically unless `-SkipSeaRaft` is explicitly supplied.
- `setup_sea_raft.ps1` creates an isolated Python 3.10-3.13 venv.
- CUDA PyTorch is installed separately from ordinary requirements so a generic PyPI dependency cannot silently replace it with a CPU-only build.
- The ordinary SEA-RAFT dependency list intentionally excludes `torch`, `torchvision`, and `torchaudio`.
- Official Spring-S and Spring-M Hugging Face model caches are prefetched into `dist/sea_raft/models`.
- The worker can load local `model.safetensors` directories directly, allowing offline conversion after a successful build/setup.
- Mainland-China setup is mirror-first for Git/PyPI/Hugging Face with official upstream fallback.

## Tunable neural motion UI

When `SEA-RAFT - Tunable Neural (CUDA)` is selected, Crow exposes:

- Model: Spring-S / Spring-M.
- Inference resolution: quarter (`scale=-2`), half (`scale=-1`), full (`scale=0`).
- Refinement iterations: 1-12, default 4.
- Neural Flow Trust: 0.50-1.50.
- Uncertainty Sensitivity: 0.25-2.50.

SEA-RAFT continues to bypass NVOF-only spatial pyramid, medoid repair and affine fallback. Backend-neutral NR-safe confidence handling and optional FG 3F/5F temporal stabilization remain available.

## UI fixes

- Parameter labels reserve a DPI-aware two-line height and standard parameter rows use a larger canonical vertical step, preventing long parameter names from clipping into a second hidden line.
- `WM_SIZE` / `WM_DPICHANGED` now invalidate and redraw all preview surfaces and the full preview region after layout changes. Minimized windows skip the forced synchronous preview refresh. This targets stale-frame/control ghosts observed after maximize/fullscreen transitions.

## Build script cleanup

- Removed shipped `pause` / `Press any key` behavior so unattended build/setup flows are not blocked at completion.
- DLSSNR selection is labeled generically as `DLSSNR runtime`. The build separately warns that RTX 40-series users require the special runtime version.
- Mainland build messaging now accurately describes the mirror-first policy and the official CUDA-PyTorch fallback.

## Validation

- Python / contract regression suite: 41 passed before final packaging.
- `sea_raft_video.py` and `scripts/portable_stage.py` pass Python syntax compilation.
- NVOF postprocess, spatial consensus and temporal consensus C++20 tests compile and run under Clang with AddressSanitizer + UndefinedBehaviorSanitizer enabled.
- Windows/MSVC, CUDA SEA-RAFT inference, build-integrated installation, and fullscreen repaint still require final RTX/Windows real-machine verification.
