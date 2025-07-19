![DEVELOPMENT](https://img.shields.io/badge/Status-DEVELOPMENT,STABLE-yellow?style=for-the-badge)

Starting this as a neat project, driven by curiosity and 1 person telling me to actually start it.

Currently super barebones - has a 2 stage bootloader (if this ever continues, bcd will be incorporated into the second bootloader).

Stage 1 -> Stage 2 -> Kernel.

Tested with QEMU, built with gcc ffreestanding for no runtime libraries, and stripped to bare metal using objcopy.

Use this and the linker script at kernel/ to compile&link. - https://github.com/lordmilko/i686-elf-tools


**SUPPORTED FEATURES:**

`Bugcheck - Half Bugcheck support, doesn't write to disk a minidump like windows, but does show a bugcheck screen, and halts the system`

`Paging - Virtual Memory Paging with permissions, like PAGE_PRESENT (is the page even mapped to physical memory?), PAGE_RW (Is page read/write or only read?), PAGE_USER (Is the page both for user mode and kernel mode, or only kernel mode?)`

`Interrupts (with full exception management) -- basic keyboard interrupt to write to screen, as well as a basic interrupt timer.`

`Memory Managment -- Dynamic Memory allocation, currently supports 128MiB max, but you may change that definition based on your system limit. (4GiB limit for now, this is 32 bit protected mode)`

`VGA Output buffer, writing to VGA video memory -> has printf (myos_printf), or basic writing.`

