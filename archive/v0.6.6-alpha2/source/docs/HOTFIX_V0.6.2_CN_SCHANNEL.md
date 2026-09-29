# V0.6.2 (40-Series Compatibility) CN AutoBuild Schannel hotfix

This hotfix does not modify DLSSNR, D3D12, Full HQ Temporal Denoise, AV1, DIS Flow, or Auto Depth processing code.

Changes:

- Mainland download helpers retry Windows `curl.exe` with `--ssl-revoke-best-effort` after a Schannel revocation-endpoint failure such as `CRYPT_E_REVOCATION_OFFLINE (0x80092013)`.
- The fallback does **not** use `--ssl-no-revoke`; TLS certificate-chain validation remains enabled.
- `Invoke-WebRequest` remains the final downloader fallback.
- FFmpeg mirror failure no longer aborts the entire source build after compilation succeeded. Runtime Self-Test and image processing can continue; video remains unavailable until FFmpeg is supplied.
- Experimental AutoBuild now tells RTX40 users to cancel the generic runtime picker and use `RTX40_RUNTIME_IMPORT.bat` after build, so `dlssnr-compat.ini` is created deliberately.
- Final verification now checks `Crow-DLSS5-Video-Image-Converter-Runtime-Self-Test.exe`.

Stable rollback baseline remains V0.6.1.2.
