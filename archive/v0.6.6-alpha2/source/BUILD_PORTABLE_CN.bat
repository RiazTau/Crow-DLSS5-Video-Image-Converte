@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title Crow-DLSS5-Video-Image-Converter V0.6.6-alpha2 - Portable CN Build

echo ============================================================
echo  Crow-DLSS5-Video-Image-Converter V0.6.6-alpha2 - PORTABLE CN BUILD
echo ============================================================
echo.
echo Creates:
echo   portable\Crow-DLSS5-Video-Image-Converter-V0.6.6-alpha2-Portable-x64\
echo   portable\Crow-DLSS5-Video-Image-Converter-V0.6.6-alpha2-Portable-x64.zip
echo.
echo The NVIDIA Optical Flow SDK is build-time only and is NOT copied.
echo The target PC still requires a supported NVIDIA display driver.
echo nvngx_dlssnr.dll is never auto-downloaded; if absent, a manual file picker will ask for it.
echo.
set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%PS%" set "PS=pwsh.exe"
"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\portable_build_cn.ps1" %*
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" (
  echo.
  echo [ERROR] Portable CN build failed with code %RC%.
  pause
  exit /b %RC%
)
echo.
echo Portable CN build completed successfully.
pause
exit /b 0
