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
#include "../cpu.h"

/// Deferred procedure calls (Like in Windows - https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/introduction-to-dpcs)
/// Basically, they are function calls that happen in the interrupt service routine, in order to lower IRQL instead of staying in such a high IRQL.
/// And to manage things that happen inside of the service routine effictively.em.
void init_dpc_system(void);

/// Enqueue DPC for deferred exceution
/// Safe to call at IRQL <= CLOCK_LEVEL, anything else will bugcheck with IRQL_NOT_LESS_OR_EQUAL.
void queue_dpc(DPC* dpc);

/// Walk through the DPC queue, raising the IRQL to DISPATCH_LEVEL in the process.
/// This gets called in a kernel idle (main) function thread.
void DispatchDPC(void);

#endif