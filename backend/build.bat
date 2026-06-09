@echo off
cd /d "%~dp0"
echo Building backend...
go build -o backend.exe ./main
if %ERRORLEVEL% neq 0 (
    echo Build failed!
    exit /b %ERRORLEVEL%
)
echo Build succeeded: backend.exe
