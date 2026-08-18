@echo off
setlocal

set SCRIPT_DIR=%~dp0

if "%~1"=="" (
    echo Usage: build_xbox.bat ^<sp^|mp^|all^> [clean]
    exit /b 1
)

set TARGET=%~1
set CLEAN_ARG=

if /I "%~2"=="clean" set CLEAN_ARG=-Clean
if /I "%~1"=="clean" (
    echo Usage: build_xbox.bat ^<sp^|mp^|all^> clean
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%scripts\build_xbox.ps1" -Target %TARGET% %CLEAN_ARG%
exit /b %ERRORLEVEL%
