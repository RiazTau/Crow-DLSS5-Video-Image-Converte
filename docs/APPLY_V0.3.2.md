# Apply V0.3.2 preview hotfix over V0.3.1

Copy `src/gui/GuiApp.cpp` from this hotfix over the same file in the existing V0.3.1 project.

Keep the existing `.deps` and `dist/runtime/nvngx_dlssnr.dll` directories.

Then rebuild:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\build.ps1 -Clean
```

Expected GUI interaction after build:

- maximize/restore: preview panes remain bounded and redraw cleanly;
- wheel over Original or DLSS5: both views zoom together;
- drag either main image while zoomed: both views pan together;
- wheel/drag Depth Guidance: independent depth inspection;
- double-click a preview: reset that preview group to Fit.
