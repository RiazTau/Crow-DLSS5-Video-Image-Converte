@echo off
setlocal
cd /d "%~dp0"
if not exist "dist\Crow-DLSS5-Video-Image-Converter-NVOF-Execute-Self-Test.exe" (
  echo [ERROR] dist\Crow-DLSS5-Video-Image-Converter-NVOF-Execute-Self-Test.exe not found.
  echo Build V0.6.6-alpha2 first with AUTO_BUILD_CN.bat or AUTO_BUILD.bat.
  pause
  exit /b 2
)
"dist\Crow-DLSS5-Video-Image-Converter-NVOF-Execute-Self-Test.exe"
set RC=%ERRORLEVEL%
echo.
if "%RC%"=="0" (
  echo [PASS] Native NvOFExecuteD3D12 synthetic-motion validation passed.
) else (
  echo [FAIL] NVOF Execute Self-Test exit code: %RC%
)
pause
exit /b %RC%
