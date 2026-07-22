@echo off
set "BUILD_PYTHON=%~dp0build_environment\python\Scripts\python.exe"
if not exist "%BUILD_PYTHON%" (
    echo RESET ERROR: Run "%~dp0initial_setup.bat" first.
    exit /b 1
)
"%BUILD_PYTHON%" "%~dp0tools\windows\reset_intellisense.py"
exit /b %ERRORLEVEL%
