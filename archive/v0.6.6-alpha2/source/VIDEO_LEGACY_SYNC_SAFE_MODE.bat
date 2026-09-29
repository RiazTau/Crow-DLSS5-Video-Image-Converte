@echo off
setlocal
cd /d "%~dp0"
set "DLSS5_DISABLE_D3D12_BATCH=1"
set "DLSS5_PERF_THREADS=1"
if not exist "dist\Crow-DLSS5-Video-Image-Converter-Video.exe" (
  echo [ERROR] dist\Crow-DLSS5-Video-Image-Converter-Video.exe not found. Run AUTO_BUILD.bat or AUTO_BUILD_CN.bat first.
  pause
  exit /b 1
)
echo V0.6.2-style safe mode: legacy D3D12 synchronization + single-thread temporal CPU loops.
"dist\Crow-DLSS5-Video-Image-Converter-Video.exe"
endlocal
