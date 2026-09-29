@echo off
setlocal
cd /d "%~dp0"
set "CROW_VIDEO_EXECUTION_MODE=legacy"
set "DLSS5_DISABLE_D3D12_BATCH=1"
set "DLSS5_PERF_THREADS=1"
if not exist "%~dp0..\Crow-DLSS-Rendering-Tool.exe" (
  echo [ERROR] Main Crow - DLSS Rendering Tool EXE is missing from the portable/dist root.

  exit /b 1
)
start "Crow - DLSS Rendering Tool - Legacy Safe" "%~dp0..\Crow-DLSS-Rendering-Tool.exe"
