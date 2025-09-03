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
#include <stdatomic.h>
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

/* Uncomment to show all reminders in a static assertion */
///#define REMINDER

/* To define DEBUG globally, use a compiler flag. I removed this since I now transitioned each header to iself and others instead of relying on kernel.h that caused circular includes. */

#define UNREFERENCED_PARAMETER(x) (void)(x)
#include "assert.h"
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
#include "drivers/ahci/ahci.h"
#include "drivers/gop/gop.h"
#include "cpu/cpuid/cpuid.h"
#include "time.h"

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

#define gop_printf_forced(color, fmt, ...) gop_printf(color, fmt, ##__VA_ARGS__)

#define ALLOCATIONS 1000
#define BLOCK_SIZE  128
#define ALIGNMENT   16

/// Memory test to run to check for memory issues - identified a problem.
static int MemoryTest(void) {
    void* blocks[ALLOCATIONS];

    // Allocation + write test
    for (int i = 0; i < ALLOCATIONS; i++) {
        blocks[i] = MtAllocateVirtualMemory(BLOCK_SIZE, ALIGNMENT);
        if (!blocks[i]) {
            gop_printf_forced(0xFFFF0000, "Allocation failed at index %d\n", i);
            return -1;
        }

        // Alignment test
        if ((uintptr_t)blocks[i] % ALIGNMENT != 0) {
            gop_printf_forced(0xFFFF8000, "Misaligned block at index %d: %p\n", i, blocks[i]);
            return -2;
        }

        // Fill memory
        for (int j = 0; j < BLOCK_SIZE; j++) {
            ((uint8_t*)blocks[i])[j] = (uint8_t)(i + j);
        }
    }

    // Verify and free
    for (int i = 0; i < ALLOCATIONS; i++) {
        for (int j = 0; j < BLOCK_SIZE; j++) {
            if (((uint8_t*)blocks[i])[j] != (uint8_t)(i + j)) {
                gop_printf_forced(0xFF0000FF, "Memory corruption at block %d, byte %d\n", i, j);
                return -3;
            }
        }

        MtFreeVirtualMemory(blocks[i]);
    }

    gop_printf_forced(0xFF00FF00, "Memory test completed successfully.\n");
    return 0;
}

#endif // X86_KERNEL_H
