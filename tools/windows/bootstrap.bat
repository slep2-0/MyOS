@echo off
echo tools\windows\bootstrap.bat has moved to initial_setup.bat.
call "%~dp0..\..\initial_setup.bat"
exit /b %ERRORLEVEL%
