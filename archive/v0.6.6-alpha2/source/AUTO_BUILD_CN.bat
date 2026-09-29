@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title Crow-DLSS5-Video-Image-Converter - Mainland China Mirror AutoBuild

echo ============================================================
echo  Crow-DLSS5-Video-Image-Converter V0.6.6-alpha2 - Native NVOF D3D12 Execute / Adaptive Stable Motion - CN Mirror AutoBuild
echo ============================================================
echo.
echo Automatic downloads use mainland-accessible mirror services only.
echo Visual Studio / Windows SDK / VC++ Runtime / NVIDIA Driver are
echo detected but may require manual installation from the vendor.
echo.

set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%PS%" (
    where pwsh.exe >nul 2>nul
    if errorlevel 1 (
        echo [ERROR] PowerShell was not found.
        echo Enable Windows PowerShell or install PowerShell, then retry.
        pause
        exit /b 10
    )
    set "PS=pwsh.exe"
)

"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\auto_build_cn.ps1" %*
set "RC=%ERRORLEVEL%"

if not "%RC%"=="0" (
    echo.
    echo ============================================================
    echo CN Mirror AutoBuild exited with code %RC%.
    echo.
    echo Check the newest file in:
    echo   logs\auto-build-cn-*.log
    echo.
    echo If script execution is enforced by Group Policy, open an
    echo Administrator PowerShell in this folder and try:
    echo   Set-ExecutionPolicy -Scope Process Bypass -Force
    echo   .\scripts\auto_build_cn.ps1
    echo.
    echo MachinePolicy/UserPolicy cannot be overridden by this package.
    echo ============================================================
    pause
    exit /b %RC%
)

echo.
echo CN Mirror AutoBuild completed successfully.
pause
exit /b 0
