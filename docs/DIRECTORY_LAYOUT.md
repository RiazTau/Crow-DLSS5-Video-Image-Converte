# Distribution layout

The project now exposes a single root build entry point: `BUILD.bat`.

After a build, the `dist` root is reserved for user-facing application executables.
Diagnostic/self-test executables and scripts live in `dist/tools`; runtime DLLs live in `dist/runtime`.

## Root build menu

- Full build (CN mirrors)
- Full build (official/global)
- FG standalone build (CN mirrors)
- FG standalone build (official/global)
- Portable builds
- NVIDIA Optical Flow SDK configuration
- DLSS-G runtime staging

Do not reintroduce separate root BAT launchers for these operations; add new build actions to `BUILD.bat` instead.
