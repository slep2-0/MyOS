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

//FIXME TODO REMOVE!!!!!!! -- All of the other function that use this also use implicit declarations, when kernel is ready, remove and fix.
/* Color definitions */
#define COLOR_BLACK 0x0
#define COLOR_BLUE 0x1
#define COLOR_GREEN 0x2
#define COLOR_CYAN 0x3
#define COLOR_RED 0x4
#define COLOR_MAGENTA 0x5
#define COLOR_BROWN 0x6
#define COLOR_LIGHT_GRAY 0x7
#define COLOR_DARK_GRAY 0x8
#define COLOR_LIGHT_BLUE 0x9
#define COLOR_LIGHT_GREEN 0xA
#define COLOR_LIGHT_CYAN 0xB
#define COLOR_LIGHT_RED 0xC
#define COLOR_LIGHT_MAGENTA 0xD
#define COLOR_YELLOW 0xE
#define COLOR_WHITE 0xF

#define UNREFERENCED_PARAMETER(x) (void)(x)

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "memory/allocator/uefi_memory.h"
#include "memory/memory.h"
#include "memory/paging/paging.h"
#include "defs/stdarg_myos.h"
#include "interrupts/idt.h"
#include "intrin/intrin.h"
#include "interrupts/handlers/handlers.h"
#include "bugcheck/bugcheck.h"
#include "memory/allocator/allocator.h"
#include "drivers/blk/block.h"
#include "drivers/blk/ata.h"
#include "drivers/gop/gop.h"
#include "filesystem/fat32/fat32.h"

// Entry point in C
void kernel_main(BOOT_INFO* boot_info);

#endif // X86_KERNEL_H
