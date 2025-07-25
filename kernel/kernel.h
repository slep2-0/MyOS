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

// Trace last func globals

#define LASTFUNC_BUFFER_SIZE 128
#define LASTFUNC_HISTORY_SIZE 10

typedef struct _LASTFUNC_HISTORY {
    uint8_t names[LASTFUNC_HISTORY_SIZE][LASTFUNC_BUFFER_SIZE];
    int current_index;
} LASTFUNC_HISTORY;

typedef struct _BLOCK_DEVICE BLOCK_DEVICE;
typedef struct _BOOT_INFO BOOT_INFO;
typedef struct _REGS REGS;

extern LASTFUNC_HISTORY lastfunc_history; // grab lastfunc from kernel.c

// Standard globals
extern bool isBugChecking;

// Custom header inline functions.
static inline void tracelast_func(const char* function_name) {
    if (!function_name) return;
    if (isBugChecking) return;

    lastfunc_history.current_index = (lastfunc_history.current_index + 1) % LASTFUNC_HISTORY_SIZE; // basically 0-9, once it reaches 10 it goes to 0 again, due to the modulus (%) operator. (because the result of this % 10 is even, which doesn't leave num after the dot (.num), so it's 0)

    // Copy new function name into the circular buffer slot
    size_t i = 0;
    while (function_name[i] != '\0' && i < LASTFUNC_BUFFER_SIZE - 1) {
        lastfunc_history.names[lastfunc_history.current_index][i] = (uint8_t)function_name[i];
        i++;
    }
    lastfunc_history.names[lastfunc_history.current_index][i] = '\0';  // null terminate
}

/* Uncomment to trigger a bugcheck on entry */
//#define CAUSE_BUGCHECK

/* Uncomment to enable debug prints */
#define DEBUG

#define UNREFERENCED_PARAMETER(x) (void)(x)

#include "intrin/intrin.h"
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
#include "irql/irql.h"

// Entry point in C
void kernel_main(BOOT_INFO* boot_info);

// Custom assembly functions externals.
extern void read_registers(REGS* registers);

#endif // X86_KERNEL_H
