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

__attribute__((noreturn))
void __stack_chk_fail(void);

// Standard globals
extern bool isBugChecking;
extern LASTFUNC_HISTORY lastfunc_history; // grab lastfunc from kernel.c

/* Definitions that change kernel behaviour below */

/* Uncomment to trigger a bugcheck on entry */
///#define CAUSE_BUGCHECK

/* Uncomment to show all reminders in a static assertion */
///#define REMINDER

/* Uncomment to disable CPU Caching */
///#define DISABLE_CACHE

/* To define DEBUG globally, use a compiler flag. I removed this since I now transitioned each header to iself and others instead of relying on kernel.h that caused circular includes. */

#define UNREFERENCED_PARAMETER(x) (void)(x)
#include "assert.h"
#include "intrinsics/intrin.h"
#include "cpu/cpu.h"
#include "filesystem/fat32/fat32.h"
#include "core/uefi_memory.h"
#include "core/memory/memory.h"
#include "core/memory/paging/paging.h"
#include "includes/stdarg_myos.h"
#include "core/interrupts/idt.h"
#include "core/interrupts/handlers/handlers.h"
#include "core/bugcheck/bugcheck.h"
#include "core/memory/allocator/allocator.h"
#include "drivers/blk/block.h"
#include "drivers/ahci/ahci.h"
#include "drivers/gop/gop.h"
#include "cpu/cpuid/cpuid.h"
#include "time.h"
#include "filesystem/vfs/vfs.h"
#include "cpu/apic/apic.h"
#include "core/mutex/mutex.h"
#include "core/events/events.h"
#include "debug/debugfunctions.h"
#include "cpu/smp/smp.h"
#include "core/acpi/acpi.h"
#include "core/process/process.h"

// Entry point in C
void kernel_idle_checks(void);
void kernel_main(BOOT_INFO* boot_info);
// Function declarations.
void copy_memory_map(BOOT_INFO* boot_info);
void copy_gop(BOOT_INFO* boot_info);
void init_boot_info(BOOT_INFO* boot_info);

// Custom assembly functions externals.
extern void read_context_frame(CTX_FRAME* registers);
extern void read_interrupt_frame(INT_FRAME* intfr);

// Initialize per CPU control registers (CR)
void InitialiseControlRegisters(void);

#define gop_printf_forced(color, fmt, ...) gop_printf(color, fmt, ##__VA_ARGS__)

#endif // X86_KERNEL_H
