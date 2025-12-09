/* atomic.h
 * Fully featured interlocked/atomic helpers for MatanelOS kernel.
 *
 * - Uses GCC/Clang __atomic builtins (x86_64).
 * - Default memory ordering: sequentially consistent (__ATOMIC_SEQ_CST).
 * - Naming follows Windows-style "Interlocked" but includes unsigned variants.
 *
 * Semantics summary:
 *  - Exchange functions return the previous value.
 *  - CompareExchange returns the initial value that was at target (Windows style).
 *  - Add/Increment/Decrement return the **new** value (matching InterlockedAdd semantics).
 *  - And/Or return the **previous** value (matching Windows InterlockedAnd/Or).
 *
 * Note: The CPU treats bit patterns the same for signed/unsigned. Use unsigned forms
 * for bitmasks/flags to avoid sign confusion in caller code.
 */

#ifndef X86_ATOMIC_H
#define X86_ATOMIC_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "../includes/annotations.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ATOMIC_ORDER __ATOMIC_SEQ_CST

   /* Exchange (returns previous value) */
    FORCEINLINE int8_t  InterlockedExchange8(volatile int8_t* target, int8_t  value) { return __atomic_exchange_n(target, value, ATOMIC_ORDER); }
    FORCEINLINE int16_t InterlockedExchange16(volatile int16_t* target, int16_t value) { return __atomic_exchange_n(target, value, ATOMIC_ORDER); }
    FORCEINLINE int32_t InterlockedExchange32(volatile int32_t* target, int32_t value) { return __atomic_exchange_n(target, value, ATOMIC_ORDER); }
    FORCEINLINE int64_t InterlockedExchange64(volatile int64_t* target, int64_t value) { return __atomic_exchange_n(target, value, ATOMIC_ORDER); }

    FORCEINLINE uint8_t  InterlockedExchangeU8(volatile uint8_t* target, uint8_t  value) { return __atomic_exchange_n(target, value, ATOMIC_ORDER); }
    FORCEINLINE uint16_t InterlockedExchangeU16(volatile uint16_t* target, uint16_t value) { return __atomic_exchange_n(target, value, ATOMIC_ORDER); }
    FORCEINLINE uint32_t InterlockedExchangeU32(volatile uint32_t* target, uint32_t value) { return __atomic_exchange_n(target, value, ATOMIC_ORDER); }
    FORCEINLINE uint64_t InterlockedExchangeU64(volatile uint64_t* target, uint64_t value) { return __atomic_exchange_n(target, value, ATOMIC_ORDER); }

    /* Pointer exchange */
    FORCEINLINE void* InterlockedExchangePtr(volatile void* volatile* target, void* value) {
        return __atomic_exchange_n((void* volatile*)target, value, ATOMIC_ORDER);
    }

    /* CompareExchange (returns initial value that was at target) */
    FORCEINLINE int8_t  InterlockedCompareExchange8(volatile int8_t* target, int8_t  value, int8_t  comparand) {
        int8_t expected = comparand;
        __atomic_compare_exchange_n(target, &expected, value, 0, ATOMIC_ORDER, ATOMIC_ORDER);
        return expected;
    }
    FORCEINLINE int16_t InterlockedCompareExchange16(volatile int16_t* target, int16_t value, int16_t comparand) {
        int16_t expected = comparand;
        __atomic_compare_exchange_n(target, &expected, value, 0, ATOMIC_ORDER, ATOMIC_ORDER);
        return expected;
    }
    FORCEINLINE int32_t InterlockedCompareExchange32(volatile int32_t* target, int32_t value, int32_t comparand) {
        int32_t expected = comparand;
        __atomic_compare_exchange_n(target, &expected, value, 0, ATOMIC_ORDER, ATOMIC_ORDER);
        return expected;
    }
    FORCEINLINE int64_t InterlockedCompareExchange64(volatile int64_t* target, int64_t value, int64_t comparand) {
        int64_t expected = comparand;
        __atomic_compare_exchange_n(target, &expected, value, 0, ATOMIC_ORDER, ATOMIC_ORDER);
        return expected;
    }

    FORCEINLINE uint8_t  InterlockedCompareExchangeU8(volatile uint8_t* target, uint8_t  value, uint8_t  comparand) {
        uint8_t expected = comparand;
        __atomic_compare_exchange_n(target, &expected, value, 0, ATOMIC_ORDER, ATOMIC_ORDER);
        return expected;
    }
    FORCEINLINE uint16_t InterlockedCompareExchangeU16(volatile uint16_t* target, uint16_t value, uint16_t comparand) {
        uint16_t expected = comparand;
        __atomic_compare_exchange_n(target, &expected, value, 0, ATOMIC_ORDER, ATOMIC_ORDER);
        return expected;
    }
    FORCEINLINE uint32_t InterlockedCompareExchangeU32(volatile uint32_t* target, uint32_t value, uint32_t comparand) {
        uint32_t expected = comparand;
        __atomic_compare_exchange_n(target, &expected, value, 0, ATOMIC_ORDER, ATOMIC_ORDER);
        return expected;
    }
    FORCEINLINE uint64_t InterlockedCompareExchangeU64(volatile uint64_t* target, uint64_t value, uint64_t comparand) {
        uint64_t expected = comparand;
        __atomic_compare_exchange_n(target, &expected, value, 0, ATOMIC_ORDER, ATOMIC_ORDER);
        return expected;
    }

    /* Pointer CompareExchange */
    FORCEINLINE void* InterlockedCompareExchangePtr(volatile void* volatile* target, void* value, void* comparand) {
        void* expected = comparand;
        __atomic_compare_exchange_n((void* volatile*)target, &expected, value, 0, ATOMIC_ORDER, ATOMIC_ORDER);
        return expected;
    }

    /* -------------------------------
       Add / Inc / Dec  (return NEW value)
       ------------------------------- */
    FORCEINLINE int8_t  InterlockedAdd8(volatile int8_t* target, int8_t  value) { return __atomic_add_fetch(target, value, ATOMIC_ORDER); }
    FORCEINLINE int16_t InterlockedAdd16(volatile int16_t* target, int16_t value) { return __atomic_add_fetch(target, value, ATOMIC_ORDER); }
    FORCEINLINE int32_t InterlockedAdd32(volatile int32_t* target, int32_t value) { return __atomic_add_fetch(target, value, ATOMIC_ORDER); }
    FORCEINLINE int64_t InterlockedAdd64(volatile int64_t* target, int64_t value) { return __atomic_add_fetch(target, value, ATOMIC_ORDER); }

    FORCEINLINE uint8_t  InterlockedAddU8(volatile uint8_t* target, uint8_t  value) { return __atomic_add_fetch(target, value, ATOMIC_ORDER); }
    FORCEINLINE uint16_t InterlockedAddU16(volatile uint16_t* target, uint16_t value) { return __atomic_add_fetch(target, value, ATOMIC_ORDER); }
    FORCEINLINE uint32_t InterlockedAddU32(volatile uint32_t* target, uint32_t value) { return __atomic_add_fetch(target, value, ATOMIC_ORDER); }
    FORCEINLINE uint64_t InterlockedAddU64(volatile uint64_t* target, uint64_t value) { return __atomic_add_fetch(target, value, ATOMIC_ORDER); }

    /* Increment / Decrement convenience (return NEW value) */
    FORCEINLINE int32_t InterlockedIncrement32(volatile int32_t* target) { return InterlockedAdd32(target, 1); }
    FORCEINLINE int32_t InterlockedDecrement32(volatile int32_t* target) { return InterlockedAdd32(target, -1); }
    FORCEINLINE uint32_t InterlockedIncrementU32(volatile uint32_t* target) { return InterlockedAddU32(target, 1); }
    FORCEINLINE uint32_t InterlockedDecrementU32(volatile uint32_t* target) { return InterlockedAddU32(target, (uint32_t)-1); }

    FORCEINLINE int64_t InterlockedIncrement64(volatile int64_t* target) { return InterlockedAdd64(target, 1); }
    FORCEINLINE uint64_t InterlockedIncrementU64(volatile uint64_t* target) { return InterlockedAddU64(target, 1); }

    FORCEINLINE int64_t InterlockedDecrement64(volatile int64_t* target) { return InterlockedAdd64(target, -1); }
    FORCEINLINE uint64_t InterlockedDecrementU64(volatile uint64_t* target) { return InterlockedAddU64(target, (uint64_t)-1); }
    /* -------------------------------
       Bitwise AND / OR (return PREVIOUS value)
       ------------------------------- */
    FORCEINLINE int8_t  InterlockedAnd8(volatile int8_t* target, int8_t  value) { return __atomic_fetch_and(target, value, ATOMIC_ORDER); }
    FORCEINLINE int16_t InterlockedAnd16(volatile int16_t* target, int16_t value) { return __atomic_fetch_and(target, value, ATOMIC_ORDER); }
    FORCEINLINE int32_t InterlockedAnd32(volatile int32_t* target, int32_t value) { return __atomic_fetch_and(target, value, ATOMIC_ORDER); }
    FORCEINLINE int64_t InterlockedAnd64(volatile int64_t* target, int64_t value) { return __atomic_fetch_and(target, value, ATOMIC_ORDER); }

    FORCEINLINE uint8_t  InterlockedAndU8(volatile uint8_t* target, uint8_t  value) { return __atomic_fetch_and(target, value, ATOMIC_ORDER); }
    FORCEINLINE uint16_t InterlockedAndU16(volatile uint16_t* target, uint16_t value) { return __atomic_fetch_and(target, value, ATOMIC_ORDER); }
    FORCEINLINE uint32_t InterlockedAndU32(volatile uint32_t* target, uint32_t value) { return __atomic_fetch_and(target, value, ATOMIC_ORDER); }
    FORCEINLINE uint64_t InterlockedAndU64(volatile uint64_t* target, uint64_t value) { return __atomic_fetch_and(target, value, ATOMIC_ORDER); }

    FORCEINLINE int8_t  InterlockedOr8(volatile int8_t* target, int8_t  value) { return __atomic_fetch_or(target, value, ATOMIC_ORDER); }
    FORCEINLINE int16_t InterlockedOr16(volatile int16_t* target, int16_t value) { return __atomic_fetch_or(target, value, ATOMIC_ORDER); }
    FORCEINLINE int32_t InterlockedOr32(volatile int32_t* target, int32_t value) { return __atomic_fetch_or(target, value, ATOMIC_ORDER); }
    FORCEINLINE int64_t InterlockedOr64(volatile int64_t* target, int64_t value) { return __atomic_fetch_or(target, value, ATOMIC_ORDER); }

    FORCEINLINE uint8_t  InterlockedOrU8(volatile uint8_t* target, uint8_t  value) { return __atomic_fetch_or(target, value, ATOMIC_ORDER); }
    FORCEINLINE uint16_t InterlockedOrU16(volatile uint16_t* target, uint16_t value) { return __atomic_fetch_or(target, value, ATOMIC_ORDER); }
    FORCEINLINE uint32_t InterlockedOrU32(volatile uint32_t* target, uint32_t value) { return __atomic_fetch_or(target, value, ATOMIC_ORDER); }
    FORCEINLINE uint64_t InterlockedOrU64(volatile uint64_t* target, uint64_t value) { return __atomic_fetch_or(target, value, ATOMIC_ORDER); }

    /* -------------------------------
       Pointer / uintptr helpers
       ------------------------------- */
    FORCEINLINE uintptr_t InterlockedExchangeUintptr(volatile uintptr_t* target, uintptr_t value) {
        return __atomic_exchange_n(target, value, ATOMIC_ORDER);
    }
    FORCEINLINE uintptr_t InterlockedCompareExchangeUintptr(volatile uintptr_t* target, uintptr_t value, uintptr_t comparand) {
        uintptr_t expected = comparand;
        __atomic_compare_exchange_n(target, &expected, value, 0, ATOMIC_ORDER, ATOMIC_ORDER);
        return expected;
    }
    FORCEINLINE uintptr_t InterlockedFetchAndUintptr(volatile uintptr_t* target, uintptr_t value) {
        return __atomic_fetch_and(target, value, ATOMIC_ORDER);
    }
    FORCEINLINE uintptr_t InterlockedFetchOrUintptr(volatile uintptr_t* target, uintptr_t value) {
        return __atomic_fetch_or(target, value, ATOMIC_ORDER);
    }

    // Boolean
    FORCEINLINE bool InterlockedExchangeBool(volatile bool* target, bool value) {
        return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
    }

    /* Pointer convenience wrappers */
    FORCEINLINE void* InterlockedExchangePointer(volatile void* volatile* target, void* value) { return InterlockedExchangePtr(target, value); }
    FORCEINLINE void* InterlockedCompareExchangePointer(volatile void* volatile* target, void* value, void* comparand) { return InterlockedCompareExchangePtr(target, value, comparand); }
    FORCEINLINE void* InterlockedFetchPointer(volatile void* volatile* target) { return InterlockedCompareExchangePtr(target, NULL, NULL); }

    /* -------------------------------
       Utility: test-and-set style helpers
       ------------------------------- */

       /* Atomically set bits in a uint32_t mask and return previous mask */
    FORCEINLINE uint32_t InterlockedSetMaskU32(volatile uint32_t* target, uint32_t mask) {
        return __atomic_fetch_or(target, mask, ATOMIC_ORDER);
    }

    /* Atomically clear bits and return previous mask */
    FORCEINLINE uint32_t InterlockedClearMaskU32(volatile uint32_t* target, uint32_t mask) {
        return __atomic_fetch_and(target, ~mask, ATOMIC_ORDER);
    }

    /* -------------------------------
       Load / Store (atomic loads/stores with specified ordering)
       ------------------------------- */
    FORCEINLINE int32_t AtomicLoad32(volatile int32_t* target) {
        return __atomic_load_n(target, ATOMIC_ORDER);
    }
    FORCEINLINE void AtomicStore32(volatile int32_t* target, int32_t v) {
        __atomic_store_n(target, v, ATOMIC_ORDER);
    }
    FORCEINLINE uint32_t AtomicLoadU32(volatile uint32_t* target) {
        return __atomic_load_n(target, ATOMIC_ORDER);
    }
    FORCEINLINE void AtomicStoreU32(volatile uint32_t* target, uint32_t v) {
        __atomic_store_n(target, v, ATOMIC_ORDER);
    }


    FORCEINLINE int8_t  InterlockedFetch8(volatile int8_t* target) { return __atomic_load_n(target, ATOMIC_ORDER); }
    FORCEINLINE int16_t InterlockedFetch16(volatile int16_t* target) { return __atomic_load_n(target, ATOMIC_ORDER); }
    FORCEINLINE int32_t InterlockedFetch32(volatile int32_t* target) { return __atomic_load_n(target, ATOMIC_ORDER); }
    FORCEINLINE int64_t InterlockedFetch64(volatile int64_t* target) { return __atomic_load_n(target, ATOMIC_ORDER); }

    FORCEINLINE uint8_t  InterlockedFetchU8(volatile uint8_t* target) { return __atomic_load_n(target, ATOMIC_ORDER); }
    FORCEINLINE uint16_t InterlockedFetchU16(volatile uint16_t* target) { return __atomic_load_n(target, ATOMIC_ORDER); }
    FORCEINLINE uint32_t InterlockedFetchU32(volatile uint32_t* target) { return __atomic_load_n(target, ATOMIC_ORDER); }
    FORCEINLINE uint64_t InterlockedFetchU64(volatile uint64_t* target) { return __atomic_load_n(target, ATOMIC_ORDER); }
    /* -------------------------------
       Notes:
       - Types may be signed or unsigned; operations are bit-pattern operations.
       - On x86_64 the builtins will generate LOCK-prefixed instructions where necessary.
       - For 8/16-bit atomics be careful about alignment; prefer natural alignment for the type.
       ------------------------------- */

#ifdef __cplusplus
}
#endif

#endif /* MATANEL_ATOMIC_H */
