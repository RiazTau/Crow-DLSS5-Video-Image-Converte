# V0.7.1-alpha1 Build Center Hotfix — Superseded by Prerequisite Hotfix 3

The earlier Build Center Hotfix 2 kept NVOF configuration and DLSS-G runtime staging as separate menu actions. That interaction model is now superseded.

Current rule:

- NVOF SDK validation/selection is a mandatory phase of every Full, FG Diagnostic and Portable build.
- DLSS-G runtime acquisition/staging is mandatory and automatic when possible; otherwise the build requires a manual `nvngx_dlssg.dll` selection.
- Build Center no longer exposes separate NVOF or DLSS-G setup menu entries.
- Direct video conversion defaults to Performance Mode; legacy sync-safe mode is diagnostics-only.
