# V0.7.3-alpha1 — SEA-RAFT Runtime Hotfix5

Base: `V0.7.3-alpha1-Hotfix1-Strict-Image-Only-Rewrite`.

This hotfix intentionally leaves the Crow build system, CMake, NVOF, video/FG code, Portable flow, and Image recursive DLSS5 implementation unchanged. Production changes are limited to:

- `sea_raft/setup_sea_raft.ps1`
- `sea_raft/sea_raft_video.py`

The two files are taken from the previously validated `V0.7.2-alpha6-SR2-SEA-RAFT-Model-Load-Hotfix5` lineage.

Fixes restored:

1. PowerShell `$Args` automatic-variable collision is removed by using `$ArgumentList`.
2. SEA-RAFT venv creation is checked immediately for `.venv\\Scripts\\python.exe`.
3. CUDA PyTorch installation has CN Aliyun wheel-directory handling plus official cu130 fallback.
4. A valid CUDA `torch` installation is no longer accepted when `torchvision` is missing or broken.
5. Requirements installation keeps CN PyPI mirror-first behavior with official PyPI fallback.
6. SEA-RAFT Git checkout keeps GitCode mirror-first behavior with official GitHub fallback.
7. Hugging Face model prefetch keeps `hf-mirror` first in CN mode and retries official Hugging Face.
8. Published Hugging Face safetensors are loaded through `RAFT.from_pretrained(..., strict=False)`, matching the official compatible loader semantics instead of Crow's old direct `strict=True` state-dict load.
9. `--check-only` model-load smoke testing validates cached Spring-S and Spring-M checkpoints during setup when present.

No `BUILD.bat`, `CMakeLists.txt`, `scripts/auto_build*.ps1`, `scripts/build*.ps1`, NVOF, Portable, video, FG, or C++ SEA-RAFT bridge file is modified by this hotfix.
