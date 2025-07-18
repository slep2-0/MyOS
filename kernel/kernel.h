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

#include <stddef.h> // Standard Library from GCC Freestanding.
#include <stdbool.h> // Standard library from GCC Freestanding
#include <stdint.h> // Standard Library from GCC Freestanding.
#include "defs/stdarg_myos.h"
#include "screen/vga/vga.h"
#include "interrupts/idt.h"
#include "intrin/intrin.h"
#include "interrupts/handlers/handlers.h"
#include "memory/memory.h"
#include "memory/paging/paging.h"
#include "bugcheck/bugcheck.h"
#endif