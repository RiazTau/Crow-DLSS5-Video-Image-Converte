# V0.7.2-alpha6-sr2 — MSVC SEA-RAFT Compile Hotfix1

## Fixed

- Renamed the local `small` selector in `SeaRaftFlowSession.cpp` to `useSmallModel`. `SeaRaftFlowSession.h` includes `<Windows.h>` and the Windows SDK exposes a legacy `small` token in the MSVC preprocessing environment; the collision corrupted the declaration and caused the C2632/C2513/C2143/C2059 cascade seen while building `crow-video-gui`.
- The fix also resolves all downstream false errors involving `cfg`, `localLegacyModel`, `localHubModel`, and the model URL `push_back` expression.
- Build failures remain non-blocking (no `pause`), but `BUILD.bat` now opens the newest AutoBuild/CMake diagnostic log in Notepad on failure. Set `CROW_BUILD_NO_OPEN_LOG=1` to suppress this behavior for automation.

## Not the cause

- SEA-RAFT Python/CUDA runtime installation had not started yet when this failure occurred.
- NVAPI was compiled with `CROW_HAS_NVAPI=0` in the supplied failing log and was not responsible for the failure.
