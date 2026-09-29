@echo off
setlocal
cd /d "%~dp0"
set "CROW_VIDEO_EXECUTION_MODE=performance"
set "DLSS5_DISABLE_D3D12_BATCH="
set "DLSS5_PERF_THREADS="
if not exist "%~dp0..\Crow-DLSS-Rendering-Tool.exe" (
  echo [ERROR] Main Crow - DLSS Rendering Tool EXE is missing from the portable/dist root.

  exit /b 1
)
start "Crow - DLSS Rendering Tool - Performance" "%~dp0..\Crow-DLSS-Rendering-Tool.exe"
