@echo off
setlocal
set "EXE=%~dp0dist\Crow-DLSS5-Video-Image-Converter-Runtime-Self-Test.exe"
if not exist "%EXE%" (
  echo ERROR: Runtime self-test has not been built yet.
  echo Run AUTO_BUILD.bat or AUTO_BUILD_CN.bat first.
  pause
  exit /b 1
)
"%EXE%"
echo.
echo Exit code: %ERRORLEVEL%
pause
endlocal
