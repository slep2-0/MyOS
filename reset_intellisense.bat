@echo off
setlocal
set "PYTHONDONTWRITEBYTECODE=1"
py -3 "%~dp0tools\windows\reset_intellisense.py"
exit /b %ERRORLEVEL%
