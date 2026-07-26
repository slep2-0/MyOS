@echo off
setlocal

py -3 "%~dp0tools\windows\sync_vcxproj_files.py"
if errorlevel 1 exit /b %ERRORLEVEL%

set "CONFIGURATION=Debug"
if /I "%~1"=="Debug" (
    set "CONFIGURATION=Debug"
    goto explicit_configuration
) else if /I "%~1"=="Release" (
    set "CONFIGURATION=Release"
    goto explicit_configuration
)

py -3 "%~dp0tools\windows\build.py" build --configuration "%CONFIGURATION%" %*
exit /b %ERRORLEVEL%

:explicit_configuration
py -3 "%~dp0tools\windows\build.py" build --configuration "%CONFIGURATION%" %2 %3 %4 %5 %6 %7 %8 %9
exit /b %ERRORLEVEL%
