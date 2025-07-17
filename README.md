Starting this as a neat project, driven by curiosity and 1 person telling me to actually start it.

Currently super barebones - has a 2 stage bootloader (if this ever continues, bcd will be incorporated into the second bootloader).

Stage 1 -> Stage 2 -> Kernel.

The kernel only supports printing to VGA buffer on the screen.

Tested with QEMU, built with gcc ffreestanding for no runtime libraries.

Use this and the linker script at kernel/ to compile&link. - https://github.com/lordmilko/i686-elf-tools
