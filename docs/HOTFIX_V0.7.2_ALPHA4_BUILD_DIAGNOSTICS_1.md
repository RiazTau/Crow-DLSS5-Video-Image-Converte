# V0.7.2-alpha4 Build Diagnostics Hotfix 1

- Captures CMake configure output to dedicated log files.
- Captures verbose MSBuild/CMake build output to dedicated log files.
- On failure, prints the last 120 diagnostic lines automatically.
- Marks algorithm unit-test executables `EXCLUDE_FROM_ALL`; normal Full/FG/Portable builds are no longer blocked by test-only targets.
- Does not change NR, FG/MFG, NVOF, Spatial Consensus, Temporal Consensus, EXR, or encoding behavior.
