@echo off
setlocal
set "PYTHONDONTWRITEBYTECODE=1"
python "%~dp0tools\windows\reset_intellisense.py"
exit /b %ERRORLEVEL%
