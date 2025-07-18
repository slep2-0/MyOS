![DEVELOPMENT](https://img.shields.io/badge/Status-DEVELOPMENT,STABLE-yellow?style=for-the-badge)

Starting this as a neat project, driven by curiosity and 1 person telling me to actually start it.

Currently super barebones - has a 2 stage bootloader (if this ever continues, bcd will be incorporated into the second bootloader).

Stage 1 -> Stage 2 -> Kernel.

Tested with QEMU, built with gcc ffreestanding for no runtime libraries, and stripped to bare metal using objcopy.

Use this and the linker script at kernel/ to compile&link. - https://github.com/lordmilko/i686-elf-tools


**SUPPORTED FEATURES:**

`Bugcheck - Half Bugcheck support, doesn't write to disk a minidump like windows, but does show a bugcheck screen, and halts the system`

`Paging - Virtual Memory Paging with a basic page fault ISR handler - doesn't BSOD yet, workin on it, :)`

`Interrupts -- basic keyboard interrupt to write to screen, as well as a basic interrupt timer.`

`Memory Managment -- basic kmalloc and kfree to allocate and deallocate memory to/from the heap. -- 1MB max, can be 3.9GB if you really wish to, this is a 32bit system using protected mode after all.`

`VGA Output buffer, writing to VGA video memory -> has printf (restricted, basic writing via print_dec or print_hex with print_to_screen is preferred) , or basic writing.`

