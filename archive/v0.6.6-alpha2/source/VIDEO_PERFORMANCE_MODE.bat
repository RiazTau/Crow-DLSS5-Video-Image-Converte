@echo off
setlocal
cd /d "%~dp0"
set "DLSS5_DISABLE_D3D12_BATCH="
set "DLSS5_PERF_THREADS="
if not exist "dist\Crow-DLSS5-Video-Image-Converter-Video.exe" (
  echo [ERROR] dist\Crow-DLSS5-Video-Image-Converter-Video.exe not found. Run AUTO_BUILD.bat or AUTO_BUILD_CN.bat first.
  pause
  exit /b 1
)
echo V0.6.5.3 performance mode: D3D12 batching ON, CPU row parallelism AUTO.
"dist\Crow-DLSS5-Video-Image-Converter-Video.exe"
endlocal
