@echo off
setlocal
cd /d "%~dp0"
if exist "%~dp0dist\Crow-DLSS5-Video-Image-Converter-NVOF-Self-Test.exe" (
  "%~dp0dist\Crow-DLSS5-Video-Image-Converter-NVOF-Self-Test.exe"
) else (
  echo Crow-DLSS5-Video-Image-Converter-NVOF-Self-Test.exe not found.
  echo Build the project first with AUTO_BUILD.bat or AUTO_BUILD_CN.bat.
)
echo.
pause
