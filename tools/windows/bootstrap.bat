@echo off
setlocal EnableExtensions

where winget >nul 2>nul
if errorlevel 1 (
    echo ERROR: winget is required to install LLVM and NASM automatically.
    exit /b 1
)

if not exist "%ProgramFiles%\LLVM\bin\clang.exe" (
    echo [INSTALL] LLVM
    winget install --exact --id LLVM.LLVM --accept-package-agreements --accept-source-agreements
    if errorlevel 1 exit /b 1
)

where nasm.exe >nul 2>nul
if errorlevel 1 if not exist "%~dp0..\nasm.exe" (
    echo [INSTALL] NASM
    winget install --exact --id NASM.NASM --accept-package-agreements --accept-source-agreements
    if errorlevel 1 exit /b 1
)

where py.exe >nul 2>nul
if errorlevel 1 (
    echo ERROR: Python 3 with the py launcher is required.
    exit /b 1
)

echo [INSTALL] Python FAT32 image dependency
py -3 -m pip install --user "pyfatfs==1.1.0"
if errorlevel 1 exit /b 1

echo.
echo Windows build prerequisites are ready.
echo Run: build_windows.bat Debug
exit /b 0
