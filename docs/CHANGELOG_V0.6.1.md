# V0.6.1 — AV1 Decoder Resilience Hotfix

## Problem fixed

Some AV1 inputs could pass V0.6.0's one-frame decoder preflight but fail during the real streaming decode with repeated FFmpeg/libaom messages such as:

```text
No sequence header
Failed to decode frame: Corrupt frame detected
Bitstream not supported by this decoder
```

This failure occurs before the decoded RGBA frame reaches DLSSNR. It is an input-decoder compatibility/corruption boundary, not a Full HQ denoiser or DLSSNR Feature 18 failure.

## Decoder selection changes

- AV1 preflight increased from 1 frame to 24 real frames converted to RGBA.
- Preflight adds FFmpeg `-xerror`; any decode error invalidates the candidate even when FFmpeg can emit partial output.
- Decoder-advertisement matching now uses the actual decoder-name token instead of substring matching descriptions.
- AV1 candidate order:
  1. `libdav1d`
  2. `av1_cuvid`
  3. `av1_qsv`
  4. native `av1`
  5. `libaom-av1`
  6. FFmpeg automatic selection
- The real AV1 streaming decoder also uses `-xerror` to stop at the first corrupt/unsupported packet instead of feeding damaged temporal history.

## Diagnostics / UX

- Added `video/logs/decoder-preflight-last.log`.
- Preflight log records codec/profile/pixel format, each decoder attempt, and the selected decoder.
- GUI failure text deduplicates repeated FFmpeg stderr lines and reports the full log path.
- Actual decode failures include the selected decoder and number of frames decoded before failure.

## FFmpeg setup

- Standard `setup_video.ps1` now prefers a full static Windows FFmpeg build with `libdav1d` in addition to NVDEC/NVENC support.
- The mainland-China mirror setup remains on the existing mirror path and now prints the available AV1 decoder capabilities; if `libdav1d` is absent, V0.6.1 still exercises hardware/native/libaom candidates through the real preflight.

## Preserved without algorithm changes

- V0.6.0 Full HQ Temporal Denoise.
- DIS optical flow and scene-cut logic.
- Current->previous motion vectors passed to DLSSNR Feature 18.
- Auto Depth and temporal depth stabilization.
- Light post-NR output stabilization.
- FFmpeg raw-RGBA streaming architecture; no intermediate PNG sequence.
- Existing image-converter functionality and NGX/D3D12 processing code.


## Validation

- Added `tests/test_av1_preflight_contract.py` to guard the decoder order, 24-frame preflight, `-xerror`, dedicated log, and full-FFmpeg setup contract.
- Existing DIS optical-flow protocol regression test remains passing.
- Existing Full HQ temporal-denoiser regression test remains passing.
- Critical Full HQ / DLSSNR / temporal source files are intentionally byte-identical to the V0.6.0 baseline.

The Windows GUI / D3D12 executable still requires the project's Windows MSVC AutoBuild environment; this source package was not cross-compiled as a Windows executable in the packaging environment.
