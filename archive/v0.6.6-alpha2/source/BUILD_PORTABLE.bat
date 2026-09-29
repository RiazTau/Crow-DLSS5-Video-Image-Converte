@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title Crow-DLSS5-Video-Image-Converter V0.6.6-alpha2 - Portable Build

echo ============================================================
echo  Crow-DLSS5-Video-Image-Converter V0.6.6-alpha2 - PORTABLE BUILD
echo ============================================================
echo.
echo nvngx_dlssnr.dll is never auto-downloaded; if absent, a manual file picker will ask for it.
echo The NVIDIA Optical Flow SDK is not redistributed.
echo.
set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%PS%" set "PS=pwsh.exe"
"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\portable_build.ps1" %*
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" (
  echo.
  echo [ERROR] Portable build failed with code %RC%.
  pause
  exit /b %RC%
)
echo.
echo Portable build completed successfully.
pause
exit /b 0
