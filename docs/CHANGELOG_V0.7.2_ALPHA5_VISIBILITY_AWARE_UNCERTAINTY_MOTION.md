# Crow - DLSS Rendering Tool V0.7.2-alpha5

## Visibility-Aware Uncertainty Motion

V0.7.2-alpha5 extends the alpha4 Spatial-Temporal Dual-Path Motion pipeline without adding another parameter wall. The NVOF postprocess now keeps correspondence topology and uncertainty separate from the existing scalar confidence field.

### New per-pixel motion state

- `historyVisibility` — continuous current-to-previous source visibility.
- `disocclusionProbability` — newly revealed current-surface probability from backward-flow landing coverage and out-of-bounds reprojection.
- `occlusionProbability` — many-to-one correspondence ambiguity from forward-flow landing density.
- `motionUncertainty` — continuous evidence disagreement from cost, FB closure, photometric reprojection, topology and later cross-scale consensus.

### NR path

The NR-safe branch now uses visibility and uncertainty directly when deriving temporal history confidence. High disocclusion or very low visibility can reject history completely while leaving the current-frame vector available to NGX. This avoids treating “no valid history exists” as merely another low-confidence optical-flow sample.

### FG path

Adaptive Reliable Motion now accepts repair neighbours only when they are sufficiently visible and not excessively uncertain. A strongly disoccluded target is no longer forced onto robust global motion; it needs local compatible evidence. Depth gating remains active.

Temporal Motion Consensus stores compact visibility and uncertainty lattices with its trajectory history. Current disocclusions are excluded from 3F/5F correction, and history traversal stops when past visibility/uncertainty is unsafe.

### Spatial path

Full/half-resolution agreement now lowers motion uncertainty. A cross-scale disagreement that requires correction remains explicitly uncertain rather than becoming artificially perfect after fusion.

### Diagnostics

`video/logs/temporal-last.log` adds:

- `mean_history_visibility`
- `disoccluded_fraction`
- `occlusion_ambiguous_fraction`
- `high_uncertainty_fraction`

It also fixes the alpha4 CSV row omission so the already-declared spatial consensus columns are actually emitted.

### Build-system fixes retained in this source

1. NVAPI validation now directly checks the explicitly supplied SDK root (`nvapi.h`, `NvApiDriverSettings.h`, `amd64/nvapi64.lib`) instead of pre-defining `NVAPI_INCLUDE_DIR` before `find_path()`.
2. PowerShell native-command wrappers temporarily use non-terminating error handling and decide success from `$LASTEXITCODE`, preventing an ordinary CMake WARNING on stderr from aborting PowerShell 5.1 builds.
