# V0.6.1.1 — AV1 Codec Detection / Preflight Routing Hotfix

## Field symptom
A V0.6.1 failure dialog showed `libaom-av1` / `No sequence header` and `Decoded frames before failure: 0`, but did **not** show `Input codec: av1`, `Selected decoder`, or the AV1 preflight log. That proved the source reached FFmpeg auto decoding without entering V0.6.1 AV1 preflight.

## Root cause
The video probe requested stream fields and format duration with repeated `-show_entries` options. On the affected FFprobe/runtime combination, resolution/fps were usable while `codec_name` was left empty. V0.6.1 gated AV1 preflight on `info.codecName == "av1"`, so an empty codec name silently bypassed the decoder-selection fix.

## Fix
- Combine stream + format fields into one ffprobe `-show_entries` expression.
- Independently query `stream=codec_name` using `nokey=1` and normalize it.
- If codec name is still unavailable, inspect FFmpeg's zero-frame stream description for `Video: av1` / `av01`.
- Carry an explicit `av1Input` flag through decoder selection and the real streaming decoder instead of repeatedly depending on one metadata string.
- Failure dialogs now explicitly say when ffprobe could not provide a codec name.
- Existing V0.6.1 24-frame decoder preflight, `libdav1d -> av1_cuvid -> av1_qsv -> av1 -> libaom-av1 -> automatic` fallback, Full HQ temporal denoise, DLSSNR, optical flow, Auto Depth and encoder behavior remain unchanged.

## Expected field behavior
For the previously failing AV1 input, the GUI must now display `Checking input decoder compatibility...` followed by an AV1 preflight result. If no decoder can cleanly decode 24 frames, conversion stops before D3D12/DLSSNR and `video\logs\decoder-preflight-last.log` contains every attempt.
