# V0.5.0.1 AutoBuild hotfix

Fixes inherited AutoBuild issues in V0.5.0:

- `AUTO_BUILD.bat` is now ASCII/CRLF without UTF-8 BOM.
- UAC elevation is handled by the BAT bootstrap before PowerShell build logic.
- Calls `build.ps1` with explicit named parameters: `-Clean -Configuration Release`.
- Adds validation/error propagation for `dist\video\setup_video.ps1` and `dist\auto_depth\setup_auto_depth.ps1`.

No DLSSNR temporal/video processing algorithm code is changed by this hotfix.
