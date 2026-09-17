@echo off
setlocal
set "PYTHONDONTWRITEBYTECODE=1"
python "%~dp0tools\windows\build.py" clean
exit /b %ERRORLEVEL%
