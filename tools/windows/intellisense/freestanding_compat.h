#ifndef MATANELOS_INTELLISENSE_FREESTANDING_COMPAT_H
#define MATANELOS_INTELLISENSE_FREESTANDING_COMPAT_H

/*
 * This file is force-included by Visual Studio IntelliSense only. It teaches
 * the MSVC language service enough GNU syntax to navigate the freestanding
 * kernel. The real build never sees these parsing-only definitions.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdatomic.h>

/* Avoid collisions with intrinsic names reserved by the MSVC front end. */
#define __cli MatanelOsIntelliSenseCli
#define __stac MatanelOsIntelliSenseStac
#define __clac MatanelOsIntelliSenseClac
#define __sti MatanelOsIntelliSenseSti
#define __hlt MatanelOsIntelliSenseHlt
#define __read_cr0 MatanelOsIntelliSenseReadCr0
#define __write_cr0 MatanelOsIntelliSenseWriteCr0
#define __read_cr2 MatanelOsIntelliSenseReadCr2
#define __write_cr2 MatanelOsIntelliSenseWriteCr2
#define __read_cr3 MatanelOsIntelliSenseReadCr3
#define __write_cr3 MatanelOsIntelliSenseWriteCr3
#define __read_cr4 MatanelOsIntelliSenseReadCr4
#define __write_cr4 MatanelOsIntelliSenseWriteCr4
#define __read_cr8 MatanelOsIntelliSenseReadCr8
#define __write_cr8 MatanelOsIntelliSenseWriteCr8
#define __read_dr MatanelOsIntelliSenseReadDr
#define __write_dr MatanelOsIntelliSenseWriteDr
#define __lidt MatanelOsIntelliSenseLidt
#define __read_rflags MatanelOsIntelliSenseReadRflags
#define __write_rflags MatanelOsIntelliSenseWriteRflags
#define __inword MatanelOsIntelliSenseInWord
#define __outword MatanelOsIntelliSenseOutWord
#define __inbyte MatanelOsIntelliSenseInByte
#define __outbyte MatanelOsIntelliSenseOutByte
#define __readmsr MatanelOsIntelliSenseReadMsr
#define __writemsr MatanelOsIntelliSenseWriteMsr
#define __read_rbp MatanelOsIntelliSenseReadRbp
#define __read_rsp MatanelOsIntelliSenseReadRsp
#define __read_rip MatanelOsIntelliSenseReadRip
#define __pause MatanelOsIntelliSensePause
#define __readgsbase MatanelOsIntelliSenseReadGsBase
#define __readgsqword MatanelOsIntelliSenseReadGsQword
#define __readfsqword MatanelOsIntelliSenseReadFsQword
#define __swapgs MatanelOsIntelliSenseSwapGs
#define __rdrand64 MatanelOsIntelliSenseRdRand64
#define __rdtsc MatanelOsIntelliSenseRdTsc

#define __attribute__(...)
#define __asm__
#define __volatile__(...)
#define volatile(...)
#define asm(...)

#define _Alignof(type) __alignof(type)
#define _Static_assert(...)

#define __ATOMIC_RELAXED 0
#define __ATOMIC_CONSUME 1
#define __ATOMIC_ACQUIRE 2
#define __ATOMIC_RELEASE 3
#define __ATOMIC_ACQ_REL 4
#define __ATOMIC_SEQ_CST 5

#define __atomic_load_n(target, order) \
    ((void)(order), *(target))
#define __atomic_store_n(target, value, order) \
    ((void)(order), (void)(*(target) = (value)))
#define __atomic_exchange_n(target, value, order) \
    ((void)(order), *(target) = (value))
#define __atomic_add_fetch(target, value, order) \
    ((void)(order), *(target) + (value))
#define __atomic_fetch_and(target, value, order) \
    ((void)(value), (void)(order), *(target))
#define __atomic_fetch_or(target, value, order) \
    ((void)(value), (void)(order), *(target))
#define __atomic_compare_exchange_n(target, expected, desired, weak, success, failure) \
    ((void)(target), (void)(expected), (void)(desired), (void)(weak),                  \
     (void)(success), (void)(failure), true)
#define __atomic_fetch_add(target, value, order) \
    ((void)(value), (void)(order), *(target))

#define __sync_bool_compare_and_swap(target, old_value, new_value) \
    ((void)(target), (void)(old_value), (void)(new_value), true)
#define __sync_val_compare_and_swap(target, old_value, new_value) \
    ((void)(old_value), (void)(new_value), *(target))
#define __sync_fetch_and_and(target, value) \
    ((void)(value), *(target))
#define __sync_fetch_and_or(target, value) \
    ((void)(value), *(target))
#define __sync_lock_test_and_set(target, value) \
    ((void)(value), *(target))
#define __sync_lock_release(target) ((void)(*(target) = 0))
#define __sync_synchronize() ((void)0)

#define __builtin_bswap32(value) ((uint32_t)(value))
#define __builtin_ctzll(value) ((void)(value), 0U)
#define __builtin_expect(expression, expected) \
    ((void)(expected), (expression))
#define __builtin_frame_address(level) ((void)(level), (void*)0)
#define __builtin_offsetof(type, member) offsetof(type, member)
#define __builtin_return_address(level) ((void)(level), (void*)0)
#define __builtin_unreachable() ((void)0)

/*
 * Parse macros.h once here, then replace its host-only fallbacks. Its include
 * guard keeps source headers from restoring the unsuitable definitions.
 */
#include "macros.h"
#undef RETADDR
#define RETADDR(level) ((void)(level), (void*)0)
#undef FIELD_OFFSET
#define FIELD_OFFSET(type, field) ((uint32_t)offsetof(type, field))

#endif
