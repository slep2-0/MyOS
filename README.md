![DEVELOPMENT](https://img.shields.io/badge/Status-DEVELOPMENT,_STABLE-purple?style=for-the-badge)

Starting this as a neat project, driven by curiosity and 1 person telling me to actually start it.

Currently super barebones - has a UEFI bootloader (might incorporate BCD into it, when we have an OS better than windows that is)

UEFI Bootloader -> Kernel.

Tested with QEMU (64bit ver this time), built with gcc ffreestanding for no runtime libraries, and stripped to bare metal using objcopy -> then formatted in kali linux to a FAT32 binary (for the UEFI bootloader to load the kernel.bin).

Use GCC 4.6.3 (the one im using) with the latest binutils, along with this EDK2 that let me use a UEFI bootloader: https://github.com/tianocore/edk2


**SUPPORTED FEATURES:**
`64 BIT addressing (long mode) is now supported, along with it's equivalent features.`

`IRQLs - IRQL support has been added (almost exactly like in windows)`

`Bugcheck - Half Bugcheck support, doesn't write to disk a minidump like windows, but does show a bugcheck screen, and halts the system`

`Paging - Virtual Memory Paging with permissions, like PAGE_PRESENT (is the page even mapped to physical memory?), PAGE_RW (Is the page read/write or only read?), PAGE_USER (Is the page both for user mode and kernel mode, or only kernel mode?)`

`Interrupts (with full exception management) -- basic keyboard interrupt to write to screen, as well as a basic interrupt timer (unused, working on it).`

`Full dynamic heap memory allocation.`

**WORKING ON:**

`FAT32 FileSystem accessing in the kernel itself.`

`Scheduler & DPC's`

`Userland`

`life.`
