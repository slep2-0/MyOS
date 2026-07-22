@echo off
setlocal EnableExtensions

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\windows\setup.ps1"
set "SETUP_RESULT=%ERRORLEVEL%"

if not "%SETUP_RESULT%"=="0" (
    echo.
    echo MatanelOS setup failed. Review the error above, then run initial_setup.bat again.
)

exit /b %SETUP_RESULT%
