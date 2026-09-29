@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\setup_nvof_sdk.ps1"
set ERR=%ERRORLEVEL%
echo.
if not "%ERR%"=="0" echo NVOF SDK setup exited with code %ERR%.
pause
exit /b %ERR%
