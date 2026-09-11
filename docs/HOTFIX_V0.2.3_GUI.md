# V0.2.3 GUI compile hotfix

Fixes the Windows/MSVC GUI build errors reported after V0.2.2:

- Adds `<objbase.h>` so `CoInitializeEx`, `COINIT_MULTITHREADED`, and `CoUninitialize` are declared.
- Removes `LONG`/`int` template ambiguity in `std::max` / `std::clamp` calls involving `RECT`.
- Makes the `TextOutW`, `MoveCtrl`, and `MoveWindow` coordinates explicitly `int`, avoiding cascading Win32 overload/argument diagnostics.
- Does not change the already-working CLI/DLSSNR processing path.

Existing `.deps` and `dist/runtime/nvngx_dlssnr.dll` may be kept. Re-run `scripts\build.ps1 -Clean`.
