$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$EnvironmentDirectory = Join-Path $Root "build_environment"
$PythonEnvironment = Join-Path $EnvironmentDirectory "python"
$Manifest = Join-Path $EnvironmentDirectory "toolchain.json"

function Find-CommandPath {
    param([Parameter(Mandatory = $true)][string]$Name)

    $Command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -ne $Command) {
        return $Command.Source
    }
    return $null
}

function Find-FirstFile {
    param([AllowNull()][AllowEmptyCollection()][string[]]$Candidates)

    foreach ($Candidate in $Candidates) {
        if (-not [string]::IsNullOrWhiteSpace($Candidate) -and
            (Test-Path -LiteralPath $Candidate -PathType Leaf)) {
            return (Resolve-Path -LiteralPath $Candidate).Path
        }
    }
    return $null
}

function Install-WingetPackage {
    param(
        [Parameter(Mandatory = $true)][string]$Id,
        [Parameter(Mandatory = $true)][string]$Name
    )

    if ($null -eq (Find-CommandPath "winget.exe")) {
        throw "winget is required to install $Name. Install App Installer from Microsoft Store, then run initial_setup.bat again."
    }

    Write-Host "[INSTALL] $Name"
    & winget.exe install --exact --id $Id --silent --accept-package-agreements --accept-source-agreements
    if ($LASTEXITCODE -ne 0) {
        throw "winget failed to install $Name (exit code $LASTEXITCODE)."
    }
}

function Find-Python {
    $Launcher = Find-CommandPath "py.exe"
    if ($null -ne $Launcher) {
        $Resolved = & $Launcher -3 -c "import sys; print(sys.executable)" 2>$null
        if ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $Resolved -PathType Leaf)) {
            return (Resolve-Path -LiteralPath $Resolved).Path
        }
    }

    $Candidates = @()
    $PythonCommand = Find-CommandPath "python.exe"
    if ($null -ne $PythonCommand) {
        $Candidates += $PythonCommand
    }
    $PythonRoot = Join-Path $env:LocalAppData "Programs\Python"
    if (Test-Path -LiteralPath $PythonRoot -PathType Container) {
        $Candidates += Get-ChildItem -LiteralPath $PythonRoot -Filter python.exe -File -Recurse |
            Sort-Object FullName -Descending |
            Select-Object -ExpandProperty FullName
    }
    return Find-FirstFile $Candidates
}

function Find-InDirectory {
    param(
        [string]$EnvironmentVariable,
        [Parameter(Mandatory = $true)][string]$FileName,
        [Parameter(Mandatory = $true)][string[]]$Fallbacks
    )

    $Candidates = @()
    if (-not [string]::IsNullOrWhiteSpace($EnvironmentVariable)) {
        $Override = [Environment]::GetEnvironmentVariable($EnvironmentVariable)
        if (-not [string]::IsNullOrWhiteSpace($Override)) {
            if (Test-Path -LiteralPath $Override -PathType Container) {
                $Candidates += Join-Path $Override $FileName
            } else {
                $Candidates += $Override
            }
        }
    }
    $Candidates += Find-CommandPath $FileName
    $Candidates += $Fallbacks
    return Find-FirstFile $Candidates
}

try {
    Write-Host "MatanelOS Windows development environment setup"
    Write-Host "Repository: $Root"
    Write-Host

    $Python = Find-Python
    if ($null -eq $Python) {
        Install-WingetPackage "Python.Python.3.13" "Python 3.13"
        $Python = Find-Python
    }
    if ($null -eq $Python) {
        throw "Python was installed but could not be located. Open a new terminal and run initial_setup.bat again."
    }

    $LlvmDirectory = Join-Path $env:ProgramFiles "LLVM\bin"
    $Clang = Find-InDirectory "LLVM_BIN" "clang.exe" @((Join-Path $LlvmDirectory "clang.exe"))
    if ($null -eq $Clang) {
        Install-WingetPackage "LLVM.LLVM" "LLVM"
    }

    $Clang = Find-InDirectory "LLVM_BIN" "clang.exe" @((Join-Path $LlvmDirectory "clang.exe"))
    $Lld = Find-InDirectory "LLVM_BIN" "ld.lld.exe" @((Join-Path $LlvmDirectory "ld.lld.exe"))
    $LldLink = Find-InDirectory "LLVM_BIN" "lld-link.exe" @((Join-Path $LlvmDirectory "lld-link.exe"))
    $Objcopy = Find-InDirectory "LLVM_BIN" "llvm-objcopy.exe" @((Join-Path $LlvmDirectory "llvm-objcopy.exe"))
    if ($null -in @($Clang, $Lld, $LldLink, $Objcopy)) {
        throw "LLVM is incomplete. clang.exe, ld.lld.exe, lld-link.exe, and llvm-objcopy.exe are all required."
    }

    $Nasm = Find-InDirectory "NASM_BIN" "nasm.exe" @((Join-Path $env:ProgramFiles "NASM\nasm.exe"))
    if ($null -eq $Nasm) {
        Install-WingetPackage "NASM.NASM" "NASM"
        $Nasm = Find-InDirectory "NASM_BIN" "nasm.exe" @((Join-Path $env:ProgramFiles "NASM\nasm.exe"))
    }
    if ($null -eq $Nasm) {
        throw "NASM was installed but nasm.exe could not be located. Open a new terminal and run initial_setup.bat again."
    }

    $QemuDirectory = Join-Path $env:ProgramFiles "qemu"
    $Qemu = Find-InDirectory "QEMU_BIN" "qemu-system-x86_64.exe" @(
        (Join-Path $QemuDirectory "qemu-system-x86_64.exe"),
        "C:\msys64\mingw64\bin\qemu-system-x86_64.exe"
    )
    if ($null -eq $Qemu) {
        Install-WingetPackage "SoftwareFreedomConservancy.QEMU" "QEMU"
        $Qemu = Find-InDirectory "QEMU_BIN" "qemu-system-x86_64.exe" @(
            (Join-Path $QemuDirectory "qemu-system-x86_64.exe")
        )
    }
    if ($null -eq $Qemu) {
        throw "QEMU was installed but qemu-system-x86_64.exe could not be located."
    }

    $QemuRoot = Split-Path -Parent $Qemu
    $OvmfDirectory = [Environment]::GetEnvironmentVariable("MATANELOS_OVMF")
    $OvmfCodeCandidates = @()
    $OvmfVarsCandidates = @()
    if (-not [string]::IsNullOrWhiteSpace($OvmfDirectory)) {
        $OvmfCodeCandidates += Join-Path $OvmfDirectory "OVMF_CODE.fd"
        $OvmfVarsCandidates += Join-Path $OvmfDirectory "OVMF_VARS.fd"
    }
    $OvmfCodeCandidates += Join-Path $QemuRoot "share\edk2-x86_64-code.fd"
    $OvmfCodeCandidates += Join-Path $QemuRoot "..\share\qemu\edk2-x86_64-code.fd"
    $OvmfVarsCandidates += Join-Path $QemuRoot "share\edk2-i386-vars.fd"
    $OvmfVarsCandidates += Join-Path $QemuRoot "..\share\qemu\edk2-i386-vars.fd"
    $OvmfCode = Find-FirstFile $OvmfCodeCandidates
    $OvmfVars = Find-FirstFile $OvmfVarsCandidates
    if ($null -in @($OvmfCode, $OvmfVars)) {
        throw "UEFI firmware was not found beside QEMU. Expected edk2-x86_64-code.fd and edk2-i386-vars.fd in QEMU's share directory."
    }

    New-Item -ItemType Directory -Force -Path $EnvironmentDirectory | Out-Null
    if (-not (Test-Path -LiteralPath (Join-Path $PythonEnvironment "Scripts\python.exe") -PathType Leaf)) {
        Write-Host "[SETUP] Creating isolated Python environment"
        & $Python -m venv $PythonEnvironment
        if ($LASTEXITCODE -ne 0) {
            throw "Python failed to create the local virtual environment."
        }
    }

    $EnvironmentPython = Join-Path $PythonEnvironment "Scripts\python.exe"
    Write-Host "[INSTALL] Python FAT32 image dependency"
    # fs 2.4 still imports pkg_resources, which setuptools 81 and newer removed.
    & $EnvironmentPython -m pip install --quiet --disable-pip-version-check "setuptools<81" "pyfatfs==1.1.0"
    if ($LASTEXITCODE -ne 0) {
        throw "pip failed to install pyfatfs."
    }
    & $EnvironmentPython -W "ignore::UserWarning" -c "from pyfatfs.PyFatFS import PyFatFS"
    if ($LASTEXITCODE -ne 0) {
        throw "pyfatfs was installed but could not be imported."
    }

    foreach ($Tool in @($Clang, $Lld, $LldLink, $Objcopy, $Nasm, $Qemu)) {
        if (-not (Test-Path -LiteralPath $Tool -PathType Leaf)) {
            throw "Required executable is missing: $Tool"
        }
    }

    $Configuration = [ordered]@{
        schema = 1
        clang = $Clang
        lld = $Lld
        lld_link = $LldLink
        objcopy = $Objcopy
        nasm = $Nasm
        qemu = $Qemu
        ovmf_code = $OvmfCode
        ovmf_vars = $OvmfVars
    }
    $Configuration | ConvertTo-Json | Set-Content -LiteralPath $Manifest -Encoding ASCII

    & $EnvironmentPython (Join-Path $PSScriptRoot "sync_vcxproj_files.py")
    if ($LASTEXITCODE -ne 0) {
        throw "Visual Studio project synchronization failed."
    }

    Write-Host
    Write-Host "[READY] MatanelOS development environment is configured."
    Write-Host "Open KernelDevelopment.sln and build Debug or Release for x64."
    Write-Host "Command-line build: build_windows.bat Debug"
    exit 0
} catch {
    if (Test-Path -LiteralPath $Manifest) {
        Remove-Item -LiteralPath $Manifest -Force
    }
    Write-Host
    Write-Error $_.Exception.Message
    exit 1
}
