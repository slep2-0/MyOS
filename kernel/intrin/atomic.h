/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      Atomic Functions for the kernel.
 */
#ifndef ATOMIC_H
#define ATOMIC_H

#include <stdint.h>

// atomic.h
// Pure GCC/C11 inline-asm implementations of Windows-like Interlocked functions

/// <summary>
/// Atomically exchanges the 32-bit value at the target with the given value.
/// </summary>
/// <param name="target">Pointer to the 32-bit value to exchange.</param>
/// <param name="value">The new 32-bit value to set.</param>
/// <returns>The previous 32-bit value stored at target.</returns>
static inline int32_t InterlockedExchange32(volatile int32_t* target, int32_t value)
{
    __asm__ volatile(
        "xchg %0, %1"
        : "+r"(value), "+m"(*target)
        :
        : "memory"
        );
    return value;
}

/// <summary>
/// Atomically exchanges the 64-bit value at the target with the given value.
/// </summary>
/// <param name="target">Pointer to the 64-bit value to exchange.</param>
/// <param name="value">The new 64-bit value to set.</param>
/// <returns>The previous 64-bit value stored at target.</returns>
static inline int64_t InterlockedExchange64(volatile int64_t* target, int64_t value)
{
    __asm__ volatile(
        "xchg %0, %1"
        : "+r"(value), "+m"(*target)
        :
        : "memory"
        );
    return value;
}

/// <summary>
/// Atomically exchanges the pointer at the target with the given pointer.
/// </summary>
/// <param name="target">Pointer to the pointer-sized value to exchange.</param>
/// <param name="value">The new pointer value to set.</param>
/// <returns>The previous pointer value stored at target.</returns>
static inline void* InterlockedExchangePtr(volatile void* volatile* target, void* value)
{
    __asm__ volatile(
        "xchg %0, %1"
        : "+r"(value), "+m"(*target)
        :
        : "memory"
        );
    return value;
}

/// <summary>
/// Compares the 32-bit value at target with comparand, and if equal, sets it to value.
/// </summary>
/// <param name="target">Pointer to the 32-bit value to compare and exchange.</param>
/// <param name="value">The new 32-bit value to set if comparison succeeds.</param>
/// <param name="comparand">The 32-bit value to compare against.</param>
/// <returns>The initial 32-bit value at target.</returns>
static inline int32_t InterlockedCompareExchange32(volatile int32_t* target, int32_t value, int32_t comparand)
{
    __asm__ volatile(
        "lock cmpxchg %2, %1"
        : "+a"(comparand), "+m"(*target)
        : "r"(value)
        : "memory"
        );
    return comparand;
}

/// <summary>
/// Compares the 64-bit value at target with comparand, and if equal, sets it to value.
/// </summary>
/// <param name="target">Pointer to the 64-bit value to compare and exchange.</param>
/// <param name="value">The new 64-bit value to set if comparison succeeds.</param>
/// <param name="comparand">The 64-bit value to compare against.</param>
/// <returns>The initial 64-bit value at target.</returns>
static inline int64_t InterlockedCompareExchange64(volatile int64_t* target, int64_t value, int64_t comparand)
{
    __asm__ volatile(
        "lock cmpxchg %2, %1"
        : "+a"(comparand), "+m"(*target)
        : "r"(value)
        : "memory"
        );
    return comparand;
}

/// <summary>
/// Compares the pointer value at target with comparand, and if equal, sets it to value.
/// </summary>
/// <param name="target">Pointer to the pointer-sized value to compare and exchange.</param>
/// <param name="value">The new pointer value to set if comparison succeeds.</param>
/// <param name="comparand">The pointer value to compare against.</param>
/// <returns>The initial pointer value at target.</returns>
static inline void* InterlockedCompareExchangePtr(volatile void* volatile* target, void* value, void* comparand)
{
    void* prev;
    __asm__ volatile(
        "lock cmpxchg %2, %1"
        : "+a"(comparand), "+m"(*target)
        : "r"(value)
        : "memory"
        );
    prev = comparand;
    return prev;
}

/// <summary>
/// Atomically adds the 32-bit value to the target and returns the result.
/// </summary>
/// <param name="target">Pointer to the 32-bit value to add to.</param>
/// <param name="value">The 32-bit value to add.</param>
/// <returns>The resulting 32-bit value after addition.</returns>
static inline int32_t InterlockedAdd32(volatile int32_t* target, int32_t value)
{
    __asm__ volatile(
        "lock xadd %0, %1"
        : "+r"(value), "+m"(*target)
        :
        : "memory"
        );
    return value + *target; /* value now holds original, so add*/
}

/// <summary>
/// Atomically increments the 32-bit value at the target by one and returns the new value.
/// </summary>
/// <param name="target">Pointer to the 32-bit value to increment.</param>
/// <returns>The new 32-bit value after increment.</returns>
static inline int32_t InterlockedIncrement32(volatile int32_t* target)
{
    return InterlockedAdd32(target, 1);
}

/// <summary>
/// Atomically decrements the 32-bit value at the target by one and returns the new value.
/// </summary>
/// <param name="target">Pointer to the 32-bit value to decrement.</param>
/// <returns>The new 32-bit value after decrement.</returns>
static inline int32_t InterlockedDecrement32(volatile int32_t* target)
{
    return InterlockedAdd32(target, -1);
}

/// <summary>
/// Atomically performs bitwise AND on the 32-bit value at target with value and returns the previous value.
/// </summary>
/// <param name="target">Pointer to the 32-bit value to AND.</param>
/// <param name="value">The 32-bit mask to AND with.</param>
/// <returns>The previous 32-bit value before the AND operation.</returns>
static inline int32_t InterlockedAnd32(volatile int32_t* target, int32_t value)
{
    __asm__ volatile(
        "lock and %1, %0"
        : "+m"(*target)
        : "r"(value)
        : "memory"
        );
    return *target;
}

/// <summary>
/// Atomically performs bitwise OR on the 32-bit value at target with value and returns the previous value.
/// </summary>
/// <param name="target">Pointer to the 32-bit value to OR.</param>
/// <param name="value">The 32-bit mask to OR with.</param>
/// <returns>The previous 32-bit value before the OR operation.</returns>
static inline int32_t InterlockedOr32(volatile int32_t* target, int32_t value)
{
    __asm__ volatile(
        "lock or %1, %0"
        : "+m"(*target)
        : "r"(value)
        : "memory"
        );
    return *target;
}

#endif /* ATOMIC_H */
