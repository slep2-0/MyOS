/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     GPLv3
 * PURPOSE:     Mutex Header Functions and Primitives.
 */

#ifndef X86_MUTEX_H
#define X86_MUTEX_H

// Include the main CPU Header (contains cpu_types.h, spinlock, etc.)
#include "../../cpu/cpu.h"

/// <summary>
/// Initialize the MUTEX variable (call only on main thread, not on a created thread) (DISPATCH MAX IRQL)
/// </summary>
/// <param name="mut">Pointer to mutex object.</param>
/// <returns>MTSTATUS Status Code.</returns>
_IRQL_requires_max_(DISPATCH_LEVEL)
MTSTATUS MtInitializeMutexObject(MUTEX* mut);

/// <summary>
/// Acquire a mutex. If a thread already acquired a mutex, this thread will enter the sleep state (will be yielded and moved from scheduler queue) (DISPATCH MAX IRQL)
/// </summary>
/// <param name="mut">Pointer to mutex object.</param>
/// <returns>MTSTATUS Status Code.</returns>
_IRQL_requires_max_(DISPATCH_LEVEL)
MTSTATUS MtAcquireMutexObject(MUTEX* mut);

/// <summary>
/// Release a mutex. If a thread tried to acquire a mutex and got put to sleep, he will be awakened and the mutex will be acquired by him. (DISPATCH MAX IRQL)
/// </summary>
/// <param name="mut">Pointer to mutex object.</param>
/// <returns>MTSTATUS Status Code.</returns>
_IRQL_requires_max_(DISPATCH_LEVEL)
MTSTATUS MtReleaseMutexObject(MUTEX* mut);

#endif