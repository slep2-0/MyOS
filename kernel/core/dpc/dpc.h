/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      DPC Types and Function Headers.
 */

#ifndef X86_DPC_H
#define X86_DPC_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "../../cpu/cpu.h"

/// Deferred procedure calls (Like in Windows - https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/introduction-to-dpcs)
/// Basically, they are function calls that happen in the interrupt service routine, in order to lower IRQL instead of staying in such a high IRQL.
/// And to manage things that happen inside of the service routine effictively.
void init_dpc_system(void);

/// Enqueue DPC for deferred exceution
/// Safe to at ANY IRQL.
void MtQueueDPC(DPC* dpc);

/// Walk through the DPC queue, raising the IRQL to DISPATCH_LEVEL in the process.
/// This gets called in a kernel idle (main) function thread.
void RetireDPCs(void);

// These 2 functions are used internally by assembly interrupts, do not use.
int MtBeginDpcProcessing(void);
void MtEndDpcProcessing(void);

#endif
