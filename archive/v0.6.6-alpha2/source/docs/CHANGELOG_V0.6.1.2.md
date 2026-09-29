# V0.6.1.2 — AV1 Metadata Sanitization / Preflight Routing Hotfix

## Field symptom

V0.6.1.1 still failed on the same AV1 input at frame 0. The error dialog contained an impossible codec value:

```text
Input codec: [libaom-av1 @ ...] failed to decode frame: bitstream not supported by this decoder
```

That proves decoder stderr had been stored as `VideoInfo.codecName`. Because the value was non-empty, the AV1 stream-sniff fallback did not run and the 24-frame decoder preflight was skipped.

## Root cause

`RunCapture` merges stdout and stderr into one pipe. V0.6.1.1's independent ffprobe fallback used `nokey=1` and accepted the first non-empty line as `codec_name`. A decoder diagnostic could therefore win before the actual machine-readable codec token.

## Fix

- Add strict `NormalizeCodecName`: only lowercase ASCII codec-token characters `[a-z0-9_.-]` and a sane length are accepted.
- Parse primary `codec_name` only after normalization.
- Add `codec_tag_string` to the structured probe.
- Replace the `nokey=1` fallback with `key=value` parsing for `codec_name` and `codec_tag_string`.
- Treat `codec_tag_string=av01` as decoder-independent AV1 evidence.
- Normalize codec metadata again at the ConvertVideo routing boundary. Invalid metadata becomes empty and therefore triggers `FfmpegStreamLooksAv1`.
- Preserve V0.6.1's 24-frame `-xerror` AV1 candidate preflight and decoder order unchanged.

## Scope isolation

No Full HQ temporal denoise, DIS flow, depth stabilization, D3D12, NGX/DLSSNR Feature 18, or encoder algorithm was modified in this hotfix.
