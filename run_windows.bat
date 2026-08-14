@echo off
setlocal
set "PYTHONDONTWRITEBYTECODE=1"

set "CONFIGURATION=Debug"
if /I "%~1"=="Debug" (
    set "CONFIGURATION=Debug"
    goto explicit_configuration
) else if /I "%~1"=="Release" (
    set "CONFIGURATION=Release"
    goto explicit_configuration
)

py -3 "%~dp0tools\windows\build.py" run --configuration "%CONFIGURATION%" --stress-mode normal %*
exit /b %ERRORLEVEL%

:explicit_configuration
py -3 "%~dp0tools\windows\build.py" run --configuration "%CONFIGURATION%" --stress-mode normal %2 %3 %4 %5 %6 %7 %8 %9
exit /b %ERRORLEVEL%
