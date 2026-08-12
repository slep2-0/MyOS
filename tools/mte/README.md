# Matanel Executable Toolchain

ELF is an intermediate object/link format only. `mte_pack.py` converts the
linked ELF into the compact image consumed by the MatanelOS loader.

## Exporting an MTDLL function

Declare a stable function once with `MTDLL_API` in
`shared/include/MatanelOS.h`. MTDLL and its consumers compile against that same
declaration:

```c
MTDLL_API bool
CloseHandle(
    HANDLE ObjectHandle
);
```

`MTDLL_API` places the implementation in `.text.mtapi`. The packer discovers
the linked symbols in that section and generates the MTE export directory.
There is no export-table assembly file to update.

Private runtime entrypoints whose signatures expose MTDLL internals remain in
`usermode/programs/dlls/mtdll/includes/exports.h`.

Only the MTDLL target defines `MATANELOS_BUILDING_MTDLL`. Position-independent
code does not imply ownership of MTDLL exports: future shared libraries remain
MTDLL consumers and receive their own explicit producer define and private
include directories from the build description.

## Native system services

Applications that require the unstable native interface include
`mtnative.h` explicitly. `MatanelOS.h` intentionally does not include it.

```c
#include <MatanelOS.h>
#include <mtnative.h>

MTSTATUS status = MtDelayExecution(false, 10);
```

`mtnative.h` is shared by the kernel, MTDLL, and user programs. Native
structures, information-class enums, and system-service declarations belong
there so the three consumers cannot silently drift apart. The native syscall
stubs in `usermode/syscalls/syscalls.asm` are emitted into `.text.mtapi`, making
them ordinary generated MTDLL exports.

## Importing an MTDLL function

Include the canonical public header and call the function normally:

```c
#include <MatanelOS.h>

bool closed = CloseHandle(handle);
```

For consumers, `MTDLL_API` expands to an ordinary external declaration. LLD
creates a PLT/GOT import only if the function is referenced. The packer converts
that dynamic ELF relocation into an MTE import entry whose IAT slot is patched
by MTDLL. There are no function-pointer declarations and no application-wide
import list.

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
