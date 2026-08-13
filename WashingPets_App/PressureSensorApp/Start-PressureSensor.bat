@echo off
set SCRIPT_DIR=%~dp0
powershell.exe -STA -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%PressureSensorApp.ps1"
