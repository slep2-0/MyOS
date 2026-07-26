# Matanel Executable Toolchain

ELF is an intermediate object/link format only. `mte_pack.py` converts the
linked ELF into the compact image consumed by the MatanelOS loader.

## Exporting an MTDLL function

Declare the function with `MTDLL_API` in an MTDLL header that is visible before
its definition:

```c
MTDLL_API bool
CloseHandle(
    HANDLE ObjectHandle
);
```

`MTDLL_API` places the implementation in `.text.mtapi`. The packer discovers
the linked symbols in that section and generates the MTE export directory.
There is no export-table assembly file to update.

## Importing an MTDLL function

Expose an ordinary function declaration in the public user-mode header:

```c
bool
CloseHandle(
    HANDLE ObjectHandle
);
```

Application code calls `CloseHandle(...)` normally. LLD creates a PLT/GOT
import only if the function is referenced. The packer converts that dynamic ELF
relocation into an MTE import entry whose IAT slot is patched by MTDLL.
There are no function-pointer declarations and no application-wide import list.

## Relocations

The packer accepts these x86-64 ELF relocation classes:

- `R_X86_64_RELATIVE`: converted from ELF virtual addresses/load-bias addends
  into MTE image-relative target and addend RVAs.
- `R_X86_64_GLOB_DAT` and `R_X86_64_JUMP_SLOT`: converted into MTE imports.

Every other dynamic relocation is rejected. The packer also rejects duplicate
targets, out-of-image RVAs, ambiguous import providers, and relocations or IAT
slots that target generated MTE metadata.

The build performs a discovery link, reserves the exact page-aligned metadata
size before BSS, relinks, and verifies that the metadata requirement did not
change. It then parses the completed MTE bytes again and compares every header,
export, import, and relocation entry with the linked ELF analysis. This keeps
BSS and every runtime RVA stable, and prevents a newly introduced allocated ELF
section from being silently omitted.

The final link also writes a `.map` file beside each intermediate `.elf`.
