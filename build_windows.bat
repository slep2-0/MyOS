@echo off
setlocal

set "BUILD_PYTHON=%~dp0build_environment\python\Scripts\python.exe"
set "TOOLCHAIN_MANIFEST=%~dp0build_environment\toolchain.json"
if not exist "%BUILD_PYTHON%" goto setup_required
if not exist "%TOOLCHAIN_MANIFEST%" goto setup_required

"%BUILD_PYTHON%" "%~dp0tools\windows\sync_vcxproj_files.py"
if errorlevel 1 exit /b %ERRORLEVEL%

set "CONFIGURATION=Debug"
if /I "%~1"=="Debug" (
    set "CONFIGURATION=Debug"
    goto explicit_configuration
) else if /I "%~1"=="Release" (
    set "CONFIGURATION=Release"
    goto explicit_configuration
)

"%BUILD_PYTHON%" "%~dp0tools\windows\build.py" build --configuration "%CONFIGURATION%" %*
exit /b %ERRORLEVEL%

:explicit_configuration
"%BUILD_PYTHON%" "%~dp0tools\windows\build.py" build --configuration "%CONFIGURATION%" %2 %3 %4 %5 %6 %7 %8 %9
exit /b %ERRORLEVEL%

:setup_required
echo BUILD ERROR: The MatanelOS build environment has not been initialized.
echo Run "%~dp0initial_setup.bat" once, then build the solution again.
exit /b 1
