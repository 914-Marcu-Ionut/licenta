@echo off
cd /d "%~dp0"
if not exist backend.exe (
    echo backend.exe not found, building first...
    call build.bat
    if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
)
echo Starting backend...
backend.exe
