# V0.7.3-alpha1 — Strict Image-Only Rewrite

Baseline: `V0.7.2-alpha6-SR2-MSVC-SEA-RAFT-Compile-Hotfix1`.

This revision intentionally leaves all build infrastructure, CN mirror configuration,
NVOF setup, SEA-RAFT setup, Portable build flow, CMake configuration, Video mode and
Frame Generation code byte-for-byte identical to the uploaded Hotfix1 baseline.

Production-code change: `src/gui/GuiApp.cpp` only.

Image Conversion adds `DLSS5 Passes` (1–8, default 1). Each completed DLSS5 image
output is fed back as the input to the next pass. The existing per-pass `Iterations`
setting remains independent.

The Image GUI minimum height is increased from 920 to 980 to keep the additional
parameter row inside the existing fixed sidebar layout.
