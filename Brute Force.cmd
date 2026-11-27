@echo off
setlocal
cd /d "%~dp0"
if not "%~1"=="" goto headless
where pythonw.exe >nul 2>nul
if errorlevel 1 (
    python.exe "%~dp0tools\launcher.py"
    if errorlevel 1 pause
) else (
    start "" pythonw.exe "%~dp0tools\launcher.py"
)
goto :eof

:headless
rem Arguments such as --prepare or --play: run without the launcher window
rem and return its exit code, for other launchers.
where python.exe >nul 2>nul
if errorlevel 1 (
    echo Missing: Python 3.10 or later on PATH. Install it, then try again: https://www.python.org/downloads/
    exit /b 9009
)
python.exe "%~dp0tools\launcher.py" %*
exit /b %errorlevel%
