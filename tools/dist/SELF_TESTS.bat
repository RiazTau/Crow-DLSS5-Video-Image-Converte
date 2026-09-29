@echo off
setlocal EnableExtensions
cd /d "%~dp0"
:MENU
cls
echo ============================================================
echo   Crow - DLSS Rendering Tool - Diagnostics
echo ============================================================
echo.
echo   [1] DLSSNR Runtime / Feature 18 Self-Test
echo   [2] NVIDIA Optical Flow Runtime Self-Test
echo   [3] NVIDIA Optical Flow Execute Self-Test
echo   [4] NVOF post-process unit test
echo   [0] Exit
echo.
set /p "CHOICE=Select: "
if "%CHOICE%"=="1" goto RUNTIME
if "%CHOICE%"=="2" goto NVOF_RUNTIME
if "%CHOICE%"=="3" goto NVOF_EXECUTE
if "%CHOICE%"=="4" goto POSTPROCESS
if "%CHOICE%"=="0" exit /b 0
goto MENU

:RUNTIME
"%~dp0Crow-DLSS-Rendering-Tool-Runtime-Self-Test.exe" --runtime "%~dp0..\runtime\nvngx_dlssnr.dll"
goto RESULT
:NVOF_RUNTIME
"%~dp0Crow-DLSS-Rendering-Tool-NVOF-Self-Test.exe"
goto RESULT
:NVOF_EXECUTE
"%~dp0Crow-DLSS-Rendering-Tool-NVOF-Execute-Self-Test.exe"
goto RESULT
:POSTPROCESS
"%~dp0test-nvof-postprocess.exe"
goto RESULT
:RESULT
set "RC=%ERRORLEVEL%"
echo.
echo Exit code: %RC%

goto MENU
