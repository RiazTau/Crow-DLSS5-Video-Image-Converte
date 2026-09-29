@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title Crow - DLSS Rendering Tool - Build Center

set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%PS%" set "PS=pwsh.exe"

if /I "%~1"=="full-cn" goto FULL_CN
if /I "%~1"=="full" goto FULL_GLOBAL
if /I "%~1"=="fg-cn" goto FG_CN
if /I "%~1"=="fg" goto FG_GLOBAL
if /I "%~1"=="portable-cn" goto PORTABLE_CN
if /I "%~1"=="portable" goto PORTABLE_GLOBAL
if /I "%~1"=="nvapi" goto NVAPI_REFRESH

:MENU
cls
echo ============================================================
echo   Crow - DLSS Rendering Tool - Unified Build Center
echo ============================================================
echo.
echo   NVOF SDK validation/selection is now a mandatory build step.
echo   DLSS-G runtime is auto-staged from NVIDIA/DLSS; if unavailable,
echo   the build will require you to select nvngx_dlssg.dll manually.
echo   SEA-RAFT CUDA runtime is installed automatically by Full/Portable builds.
echo.
echo   [1] Unified NR + FG full build - CN mirrors ^(recommended in mainland China^)
echo   [2] Unified NR + FG full build - Official/global sources
echo   [3] FG diagnostic standalone build - CN mirrors
echo   [4] FG diagnostic standalone build - Official/global sources
echo   [5] Portable build - CN mirrors
echo   [6] Portable build - Official/global sources
echo   [7] Re-download / refresh NVIDIA NVAPI SDK ^(FG Presets^)
echo   [0] Exit
echo.
set /p "CHOICE=Select: "
if "%CHOICE%"=="1" goto FULL_CN
if "%CHOICE%"=="2" goto FULL_GLOBAL
if "%CHOICE%"=="3" goto FG_CN
if "%CHOICE%"=="4" goto FG_GLOBAL
if "%CHOICE%"=="5" goto PORTABLE_CN
if "%CHOICE%"=="6" goto PORTABLE_GLOBAL
if "%CHOICE%"=="7" goto NVAPI_REFRESH
if "%CHOICE%"=="0" exit /b 0
goto MENU

:FULL_CN
call :REQUIRE_NVOF
if errorlevel 1 goto FAILED
"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\auto_build_cn.ps1"
if errorlevel 1 goto FAILED
call :FINALIZE
if errorlevel 1 goto FAILED
goto DONE

:FULL_GLOBAL
call :REQUIRE_NVOF
if errorlevel 1 goto FAILED
"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\auto_build.ps1"
if errorlevel 1 goto FAILED
call :FINALIZE
if errorlevel 1 goto FAILED
goto DONE

:FG_CN
call :REQUIRE_NVOF
if errorlevel 1 goto FAILED
"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build_fg_cn.ps1" -Clean
if errorlevel 1 goto FAILED
call :FINALIZE
if errorlevel 1 goto FAILED
goto DONE

:FG_GLOBAL
call :REQUIRE_NVOF
if errorlevel 1 goto FAILED
"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build_fg.ps1" -Clean
if errorlevel 1 goto FAILED
call :FINALIZE
if errorlevel 1 goto FAILED
goto DONE

:PORTABLE_CN
call :REQUIRE_NVOF
if errorlevel 1 goto FAILED
"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\portable_build_cn.ps1"
if errorlevel 1 goto FAILED
call :FINALIZE
if errorlevel 1 goto FAILED
goto DONE

:PORTABLE_GLOBAL
call :REQUIRE_NVOF
if errorlevel 1 goto FAILED
"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\portable_build.ps1"
if errorlevel 1 goto FAILED
call :FINALIZE
if errorlevel 1 goto FAILED
goto DONE

:NVAPI_REFRESH
echo.
echo ============================================================
echo [MAINTENANCE] NVIDIA NVAPI SDK refresh
echo ============================================================
"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\setup_nvapi_sdk.ps1" -ForceRedownload
if errorlevel 1 (
    echo.
    echo [WARN] NVAPI refresh failed. Any previously valid SDK was preserved.
    echo        Core NR / FG / MFG builds can still continue with Driver Default.

    goto MENU
)
echo.
echo [OK] NVAPI SDK refresh completed and validated.
echo      Re-run a Full/FG/Portable build to enable Preset A/B/Latest.

goto MENU

:REQUIRE_NVOF
echo.
echo ============================================================
echo [REQUIRED] NVIDIA Optical Flow SDK 5.x
echo ============================================================
"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\setup_nvof_sdk.ps1" -Required -PreferSaved
exit /b %ERRORLEVEL%

:FINALIZE
"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\finalize_dist.ps1"
exit /b %ERRORLEVEL%

:FAILED
set "BUILD_RC=%ERRORLEVEL%"
if "%BUILD_RC%"=="0" set "BUILD_RC=1"
echo.
echo ============================================================
echo [ERROR] Build operation failed. Exit code: %BUILD_RC%
echo ============================================================
echo Latest compiler/AutoBuild log will be opened in Notepad.
if /I not "%CROW_BUILD_NO_OPEN_LOG%"=="1" (
    "%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\open_latest_build_log.ps1" -Root "%~dp0"
)

exit /b %BUILD_RC%

:DONE
echo.
echo ============================================================
echo [READY] Operation completed.
echo dist root now contains application EXEs only.
echo Diagnostics and self-tests: dist\tools
echo Runtime dependencies       : dist\runtime
echo Default video execution    : PERFORMANCE
echo ============================================================

exit /b 0
