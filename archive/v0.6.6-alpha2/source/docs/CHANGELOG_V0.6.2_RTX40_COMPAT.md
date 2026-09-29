# V0.6.2 RTX40 Compatibility — Historical Development Changelog

> Status update: this branch was accepted as the official V0.6.2 mainline before V0.6.3 development.

Stable rollback baseline: **V0.6.1.2**.

This branch is deliberately additive. It does not bundle, download, patch, or redistribute any proprietary NVIDIA runtime. The user supplies a legally obtained `nvngx_dlssnr.dll`.

## Isolation rules

- No `dist/runtime/dlssnr-compat.ini` file: caller behavior remains V0.6.1.2 `legacy_hook`.
- `profile=rtx40-community` only opts into experimental runtime routing.
- `caller_mode=legacy_hook` preserves the existing signed-snippet compatibility hook.
- `caller_mode=direct` skips that hook and is intended only for community runtimes that no longer require the legacy caller gate.
- A new `Crow-DLSS5-Video-Image-Converter-Runtime-Self-Test.exe` tests `direct` and `legacy_hook` in separate child processes so a runtime crash does not destroy the parent test process.
- The self-test performs an actual 256x256 Feature 18 Create + Evaluate and reports whether the output buffer was produced.

## Why this structure

The RTX50 path is already stable in V0.6.1.2. RTX40 compatibility therefore must remain opt-in and reversible. Deleting `dist/runtime/dlssnr-compat.ini` returns the application to the original V0.6.1.2 caller mode.

## Field-test sequence

1. Build normally.
2. Run `RTX40_RUNTIME_IMPORT.bat` and select an RTX40-capable community runtime obtained by the user.
3. Run `dist/Crow-DLSS5-Video-Image-Converter-Runtime-Self-Test.exe`.
4. If `legacy_hook` passes, keep it. If only `direct` passes, run `scripts/set_rtx40_caller_mode.ps1 -CallerMode direct`.
5. Test one still image before video.
6. If both modes fail, keep V0.6.1.2 as the stable branch and do not modify its source/runtime folder.
