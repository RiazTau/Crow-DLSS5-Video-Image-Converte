@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title Crow-DLSS5-Video-Image-Converter - Automatic Source Build

echo ============================================================
echo  Crow-DLSS5-Video-Image-Converter V0.6.6-alpha2 - Native NVOF D3D12 Execute / Adaptive Stable Motion - Automatic Source Build
echo ============================================================
echo.

set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%PS%" (
    where pwsh.exe >nul 2>nul
    if errorlevel 1 (
        echo [ERROR] PowerShell was not found.
        echo Please install PowerShell or enable Windows PowerShell, then retry.
        pause
        exit /b 10
    )
    set "PS=pwsh.exe"
)

rem The --elevated marker is used only by this bootstrap after UAC relaunch.
if /I "%~1"=="--elevated" (
    shift
    goto :RUN_BUILD
)

rem fltmc succeeds only from an elevated token on normal Windows installations.
fltmc >nul 2>nul
if not errorlevel 1 goto :RUN_BUILD

echo [INFO] Administrator permission is required for automatic dependency installation.
echo [INFO] Requesting UAC elevation now...
set "AUTOBUILD_BAT=%~f0"
set "AUTOBUILD_DIR=%~dp0"
"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "try { Start-Process -FilePath $env:AUTOBUILD_BAT -ArgumentList '--elevated' -WorkingDirectory $env:AUTOBUILD_DIR -Verb RunAs ^| Out-Null; exit 0 } catch { Write-Host ('[ERROR] UAC elevation failed or was cancelled: ' + $_.Exception.Message); exit 1223 }"
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" (
    echo.
    echo ============================================================
    echo Automatic elevation failed or was cancelled. Exit code: %RC%
    echo.
    echo Manual fallback:
    echo   1. Right-click AUTO_BUILD.bat
    echo   2. Choose "Run as administrator"
    echo.
    echo If script execution is blocked by policy, open an elevated
    echo PowerShell in this folder and run:
    echo   Set-ExecutionPolicy -Scope Process Bypass -Force
    echo   .\scripts\auto_build.ps1 -NoElevation
    echo ============================================================
    pause
    exit /b %RC%
)

rem The elevated copy owns the visible build console from this point on.
exit /b 0

:RUN_BUILD
"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\auto_build.ps1" -NoElevation %*
set "RC=%ERRORLEVEL%"

if not "%RC%"=="0" (
    echo.
    echo ============================================================
    echo Automatic build exited with code %RC%.
    echo.
    echo Check the newest file in:
    echo   logs\auto-build-*.log
    echo.
    echo If PowerShell execution is enforced by Group Policy, an
    echo administrator-managed exception may be required. Process-scope
    echo Bypass cannot override MachinePolicy or UserPolicy.
    echo ============================================================
    pause
    exit /b %RC%
)

echo.
echo Automatic build completed successfully.
pause
exit /b 0
