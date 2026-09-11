# V0.6.5.3 — Adaptive Stable Motion

V0.6.5.3 is a focused automatic-motion reliability upgrade over V0.6.5.2. It does not change the Direct NGX DLSSNR Feature 18 runtime path, D3D12 batching, External EXR calibration, Auto Depth, encoding, image parameter persistence, or the 2x2 preview layout.

## Why

The V0.6.5.2 Motion Preview exposed a recurring Auto/DIS pattern: some low-motion or near-duplicate frame pairs could become a mostly black HSV field containing sparse coloured speckles. Those speckles are small DIS vectors around compression noise / edges, but because they are still valid numeric motion they can be passed to temporal reconstruction.

## Adaptive Stable DIS

The Auto/DIS helper now uses two DIS configurations:

1. **Stable candidate** — OpenCV DIS Medium with mean normalization ON and spatial propagation OFF. OpenCV documents that disabling spatial propagation can produce a smoother field.
2. **Rescue candidate** — OpenCV DIS Medium with spatial propagation ON. It is computed only when the stable candidate has low confidence / many low-confidence pixels / suspiciously large motion. OpenCV documents spatial propagation as useful for recovering major coarse-to-fine errors.

Candidate selection is based on the existing real forward/backward + photometric confidence rather than motion magnitude alone.

## Near-duplicate suppression

A frame pair is classified as near-duplicate only when all of these are true at analysis resolution:

- normalized frame MAD < 0.010;
- fewer than 2% of pixels differ by more than 8 luma levels;
- median motion < 0.12 px;
- P95 motion < 1.50 px.

For that narrow class, the final motion field is forced to exact zero and confidence is derived from residual image similarity. This prevents sparse sub-pixel noise from being interpreted as meaningful motion while avoiding suppression of ordinary camera/object motion.

## Isolated low-confidence conditioning

The helper computes a local 3x3 component median as a proposal, but does **not** globally blur motion. A vector is corrected only when:

- confidence < 0.35; and
- it disagrees strongly with its local median.

High-confidence motion boundaries are preserved. Forward and backward fields are conditioned symmetrically, then confidence is recomputed for the exact final field.

## Diagnostics

`dist/video/dis-flow-video.log` now records only noteworthy Auto/DIS events such as:

- `mode=zero-near-duplicate`
- `mode=rescue`
- very low-confidence final pairs

The binary pipe protocol remains `FRM1` / `FLW1`, so the C++ integration does not need a protocol migration.

## Reference-driven design

The design intentionally borrows principles rather than source code:

- OpenCV DIS: spatial propagation trade-off and DIS quality controls.
- NVIDIA Optical Flow: forward/backward reliability, temporal continuity, cost/reliability concepts, and global-motion fallback as future directions.
- GMFlow: bidirectional flow + forward/backward consistency for occlusion/reliability.
- SEA-RAFT: uncertainty-aware learned flow as a future HQ backend.
- MemFlow: sequence memory as a future temporal-stability direction.

See `docs/RESEARCH_OPTICAL_FLOW_BACKENDS_2026-09-04.md`.
