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

// forward declarations, i don't think i need them.

typedef struct _BLOCK_DEVICE BLOCK_DEVICE;
typedef struct _BOOT_INFO BOOT_INFO;

__attribute__((noreturn))
void __stack_chk_fail(void);

// Standard globals
extern bool isBugChecking;

/* Definitions that change kernel behaviour below */

/* Uncomment to trigger a bugcheck on entry */
///#define CAUSE_BUGCHECK

/* Uncomment to show all reminders in a static assertion */
///#define REMINDER

/* To define DEBUG globally, use a compiler flag. I removed this since I now transitioned each header to iself and others instead of relying on kernel.h that caused circular includes. */

#define UNREFERENCED_PARAMETER(x) (void)(x)
#include "includes/mtos.h"
#include "assert.h"
#include "intrinsics/intrin.h"
#include "filesystem/fat32/fat32.h"
#include "includes/stdarg_myos.h"
#include "drivers/blk/block.h"
#include "drivers/ahci/ahci.h"
#include "drivers/gop/gop.h"
#include "time.h"
#include "filesystem/vfs/vfs.h"
#include "includes/behavior.h"

// Entry point in C
void kernel_idle_checks(void);
void kernel_main(BOOT_INFO* boot_info);
// Function declarations.
void copy_memory_map(BOOT_INFO* boot_info);
void copy_gop(BOOT_INFO* boot_info);
void init_boot_info(BOOT_INFO* boot_info);

#define gop_printf_forced(color, fmt, ...) gop_printf(color, fmt, ##__VA_ARGS__)

#endif // X86_KERNEL_H
