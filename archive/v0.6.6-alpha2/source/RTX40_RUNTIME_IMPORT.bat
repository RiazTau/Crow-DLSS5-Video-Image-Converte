@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\import_runtime_rtx40.ps1"
if errorlevel 1 pause
endlocal
