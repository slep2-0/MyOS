/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     Core Kernel Includes, includes all core and necessary header files.
 */

// Main includes
#ifndef X86_KERNEL_H
#define X86_KERNEL_H

/* Comment or uncomment to cause a bugcheck upon entering the system */
//#define CAUSE_BUGCHECK

/* Comment Or Uncomment to allow debugging messages from kernel functions that support it */
//#define DEBUG

/* Custom Macros */
#define UNREFERENCED_PARAMETER(x) (void)(x) // windows style unreferenced parameter. (cast to void, void is obviously nothing, it's a void)

// INT includes.
#include <stddef.h> // Standard Library from GCC Freestanding.
#include <stdbool.h> // Standard library from GCC Freestanding
#include <stdint.h> // Standard Library from GCC Freestanding.

typedef struct _GOP_PARAMS {
    uint64_t FrameBufferBase;
    uint64_t FrameBufferSize;
    uint32_t Width;               // Visible width in pixels
    uint32_t Height;              // Visible height in pixels
    uint32_t PixelsPerScanLine;   // Actual pixels per row in framebuffer (stride)
} GOP_PARAMS;

#include "defs/stdarg_myos.h"
#include "screen/vga/vga.h" //-- UNUSED - We are now in long mode. (UEFI)
#include "interrupts/idt.h"
#include "intrin/intrin.h"
#include "interrupts/handlers/handlers.h"
#include "memory/memory.h"
#include "memory/paging/paging.h"
#include "bugcheck/bugcheck.h"
#include "memory/allocator/allocator.h"
#include "drivers/blk/block.h"
#include "drivers/blk/ata.h"
#include "filesystem/fat32/fat32.h"
#include "drivers/gop/gop.h"

// Added declaration for kernel main.
void kernel_main(GOP_PARAMS* gop);

#endif