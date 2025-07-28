/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     Core Kernel Includes, includes all core and necessary header files.
 */

#ifndef X86_KERNEL_H
#define X86_KERNEL_H

// Standard headers, required.
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "trace.h"

// forward declarations, i don't think i need them.

typedef struct _BLOCK_DEVICE BLOCK_DEVICE;
typedef struct _BOOT_INFO BOOT_INFO;
typedef struct _CTX_FRAME CTX_FRAME;


// Standard globals
extern bool isBugChecking;
extern LASTFUNC_HISTORY lastfunc_history; // grab lastfunc from kernel.c

/* Uncomment to trigger a bugcheck on entry */
///#define CAUSE_BUGCHECK

/* To define DEBUG globally, use a compiler flag. I removed this since I now transitioned each header to iself and others instead of relying on kernel.h that caused circular includes. */

#define UNREFERENCED_PARAMETER(x) (void)(x)

#include "intrin/intrin.h"
#include "cpu/cpu.h"
#include "filesystem/fat32/fat32.h"
#include "memory/allocator/uefi_memory.h"
#include "memory/memory.h"
#include "memory/paging/paging.h"
#include "defs/stdarg_myos.h"
#include "interrupts/idt.h"
#include "interrupts/handlers/handlers.h"
#include "bugcheck/bugcheck.h"
#include "memory/allocator/allocator.h"
#include "drivers/blk/block.h"
#include "drivers/blk/ata.h"
#include "drivers/gop/gop.h"

// Entry point in C
void kernel_idle_checks(void);
void kernel_main(BOOT_INFO* boot_info);
// Function declarations.
void copy_memory_map(BOOT_INFO* boot_info);
void copy_gop(BOOT_INFO* boot_info);
void init_boot_info(BOOT_INFO* boot_info);
void InitCPU(void);

// Custom assembly functions externals.
extern void read_context_frame(CTX_FRAME* registers);
extern void read_interrupt_frame(INT_FRAME* intfr);

#endif // X86_KERNEL_H
