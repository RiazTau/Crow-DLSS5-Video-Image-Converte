# Crow - DLSS Rendering Tool V0.7.2-alpha6

## Predictive Multi-Scale Motion Reconstruction

Alpha6 is a quality-focused follow-up to alpha5. Alpha5 added visibility / occlusion / disocclusion / uncertainty signals and therefore became much safer about rejecting invalid history, but user A/B feedback showed only limited visible improvement when NVOF itself selected the wrong motion phase. Alpha6 therefore strengthens motion candidate generation and reconstruction.

### Motion pyramid

- Replaced the 2x2 half-resolution box filter with a separable 5-tap binomial low-pass before decimation.
- Auto keeps full + 1/2-resolution NVOF.
- Strong adds a third 1/4-resolution NVOF session for severe periodic/repetitive textures.
- The quarter-resolution prior is fused with a reduced correction budget (`priorStrength = 0.62`).
- High full-resolution confidence only blocks coarse correction when motion uncertainty is also low.

### Dominant local mode reconstruction

- Replaced component-wise weighted neighbour mean with a weighted vector medoid.
- Only the dominant cluster around the medoid is averaged.
- Cluster coherence controls whether the local candidate is accepted and how much uncertainty can be reduced.
- Existing confidence, visibility, uncertainty and depth gates remain in force.
- Target eligibility uses effective trust (confidence + visibility + uncertainty) so a high-confidence periodic lock can still be rebuilt when independent evidence disagrees.
- Post-spatial active reconstruction now runs with or without depth; depth is an optional boundary gate instead of a prerequisite.

### Robust affine camera fallback

- Replaced the translation-only robust global median fallback with a sparse robust affine displacement fit.
- Uses high-confidence, visible, low-uncertainty samples.
- Uses IRLS / Huber-style residual reweighting.
- Handles pan, first-order zoom, rotation-like motion and mild shear.
- Remains secondary to coherent local object motion and is disabled for strongly disoccluded targets.

### Predictive temporal consensus

- Tracks history reliability using confidence * visibility * uncertainty penalty.
- Uses the newest velocity difference as a one-step acceleration prediction.
- Longer history measures acceleration agreement before trusting prediction strongly.
- Temporal residual threshold becomes uncertainty-adaptive.
- Correction blend now depends on current uncertainty versus historical reliability / uncertainty.

### Diagnostics

- `temporal-last.log` adds `local_mode_repair_fraction` and `affine_fallback_fraction`.
- Spatial consensus diagnostics accumulate sequential 1/2 + 1/4 correction activity instead of being overwritten by the last scale.

### Regression / validation

- Existing alpha5 visibility and build-system fixes are retained.
- Added unit coverage for anti-aliased quarter-scale construction and affine fallback reconstruction.
- Legacy alpha2-alpha5 contract tests accept the upgraded implementation while continuing to check the original functional guarantees.
