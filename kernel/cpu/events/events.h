/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:		 Event Headers and Prototypes (refer to KeSetEvent in windows)
 */

#ifndef X86_EVENT_H
#define X86_EVENT_H

#include "../cpu.h"

/// <summary>
/// Signals an EVENT struct to wake up threads. (Acquires Spinlock)
/// </summary>
/// <param name="Event">Pointer to EVENT variable.</param>
/// <returns>MTSTATUS Status Code.</returns>
MTSTATUS MtSetEvent(EVENT* event);

/// <summary>
/// Waits for an event to be signaled, sleeps the current thread.
/// </summary>
/// <param name="event">Pointer to EVENT variable.</param>
/// <returns>MTSTATUS Status Code.</returns>
MTSTATUS MtWaitForEvent(EVENT* event);

#endif
