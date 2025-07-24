/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     Core Kernel Includes, includes all core and necessary header files.
 */

#ifndef X86_KERNEL_H
#define X86_KERNEL_H

 /* Uncomment to trigger a bugcheck on entry */
 //#define CAUSE_BUGCHECK

 /* Uncomment to enable debug prints */
 //#define DEBUG

#define UNREFERENCED_PARAMETER(x) (void)(x)

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "memory/allocator/uefi_memory.h"
#include "memory/memory.h"
#include "memory/paging/paging.h"
#include "defs/stdarg_myos.h"
#include "screen/vga/vga.h"         // UNUSED in GOP mode
#include "interrupts/idt.h"
#include "intrin/intrin.h"
#include "interrupts/handlers/handlers.h"
#include "bugcheck/bugcheck.h"
#include "memory/allocator/allocator.h"
#include "drivers/blk/block.h"
#include "drivers/blk/ata.h"
#include "filesystem/fat32/fat32.h"
#include "drivers/gop/gop_print.h"

// Entry point in C
void kernel_main(BOOT_INFO* boot_info);

#endif // X86_KERNEL_H
