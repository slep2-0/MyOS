@echo off
py -3 "%~dp0tools\windows\build.py" clean
exit /b %ERRORLEVEL%
