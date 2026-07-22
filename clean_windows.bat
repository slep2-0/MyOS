@echo off
if exist "%~dp0build\windows" rmdir /s /q "%~dp0build\windows"
echo [CLEAN] Removed %~dp0build\windows
exit /b 0
