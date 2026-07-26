# MatanelOS Windows Build

This build runs entirely on Windows and is integrated into `KernelDevelopment.sln`.
Visual Studio is the IDE and build front end; LLVM supplies the freestanding ELF
compiler and linker used by the kernel and user-mode image formats.

## Why LLVM instead of cl.exe

The current source is written for a GNU-style freestanding x86-64 ABI. It uses
GNU attributes, GNU inline assembly, `__atomic_*`, `__sync_*`, `typeof`, ELF
linker scripts, the large code model, and a disabled red zone. Windows x64
`cl.exe` cannot directly represent several of those contracts and emits COFF,
not ELF.

`clang.exe --target=x86_64-none-elf` accepts the existing language extensions
and emits the same artifact family expected by the bootloader. This is a native
Windows executable, so Kali, WSL, Cygwin, and MSYS shells are not involved.

A later true MSVC source port is possible, but it is a separate ABI project:

- move GNU inline assembly to NASM or compiler intrinsics;
- wrap attributes, atomics, and builtins in compiler-neutral headers;
- choose COFF/PE kernel and user image layouts or add a conversion stage;
- replace GNU linker-script assumptions;
- verify calling conventions, structure packing, red-zone behavior, and the
  context-switch register contract.

## One-time setup

Run from a normal Command Prompt:

```bat
tools\windows\bootstrap.bat
```

The script installs/checks LLVM, NASM, Python, and `pyfatfs`. QEMU is optional
for compiling. The current machine's QEMU and OVMF locations are detected by
the run command.

Optional environment overrides:

```text
LLVM_BIN=C:\path\to\llvm\bin
NASM_BIN=C:\path\to\nasm\directory
QEMU_BIN=C:\path\to\qemu\directory
MATANELOS_OVMF=C:\path\containing\OVMF_CODE.fd and OVMF_VARS.fd
```

## Command line

Build the complete Debug image:

```bat
build_windows.bat
```

Other useful commands:

```bat
build_windows.bat Release
build_windows.bat Debug --target bootloader
build_windows.bat Debug --target kernel
build_windows.bat Debug --target usermode
clean_windows.bat
reset_intellisense.bat
run_windows.bat Debug --cpus 4
run_windows.bat Debug --cpus 1
```

The complete image is written to:

```text
build\windows\debug\matanelos.img
```

The image writer creates a 64 MiB GPT disk with a FAT32 EFI System Partition
and these files:

```text
EFI\BOOT\BOOTX64.EFI
kernel.elf
mtdll.mtdll
terminateMyself.mtexe
```

The bootloader is compiled from `boot\bootloader_uefi.c` and the small
freestanding support unit in `boot\uefi_runtime.c`. The generated PE32+ EFI
application is written to `build\windows\debug\bootloader.efi` (or the
corresponding Release directory) and is placed in the image as
`EFI\BOOT\BOOTX64.EFI`.

## Visual Studio

Open `KernelDevelopment.sln`, choose `Debug|x64` or `Release|x64`, and use
**Build Solution**. The project is an NMake project that invokes the same batch
file. LLVM diagnostics use Visual Studio's clickable `file(line,column)` form.

Visual Studio does not reliably support recursive wildcard items in C/C++
projects. `build_windows.bat` therefore runs `sync_vcxproj_files.py` first. The
script keeps an explicit, deterministic project file list and rewrites the
project only when a source file was added or removed. Reload the project if
Visual Studio prompts after such a change.

After changing the NMake IntelliSense definitions, close Visual Studio and run
`reset_intellisense.bat` once. It removes only this solution's stale C/C++
browsing databases; it leaves source files and Visual Studio settings intact.

The builder compiles independent translation units in parallel and reuses
objects until a source, header, configuration, or flag changes. NASM structure
offsets are generated from LLVM IR for the `x86_64-none-elf` target, so they do
not inherit Windows' LLP64 structure model.

Debug builds retain the global stack protector and exempt `kernel.c` while it
initializes the cookie, matching the established kernel build behavior. LLVM does not implement
`-fstack-clash-protection` for this bare-metal target, so that GCC-only flag is
omitted; `-Wframe-larger-than=4096` supplies the compile-time large-frame check.
