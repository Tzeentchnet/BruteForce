@echo off
setlocal
cd /d "%~dp0"
where pythonw.exe >nul 2>nul
if errorlevel 1 (
    python.exe "%~dp0tools\launcher.py"
    if errorlevel 1 pause
) else (
    start "" pythonw.exe "%~dp0tools\launcher.py"
)
