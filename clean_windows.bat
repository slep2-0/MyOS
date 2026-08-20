@echo off
setlocal
set "PYTHONDONTWRITEBYTECODE=1"
py -3 "%~dp0tools\windows\build.py" clean
exit /b %ERRORLEVEL%
