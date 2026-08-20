#ifndef X86_MATANEL_MACROS_H
#define X86_MATANEL_MACROS_H
#include <stdint.h>

/// This example is using the legacy kernel structures.
/// Usage: CONTAINING_RECORD(ptr, struct, ptr_member)
/// Example: 
/// CTX_FRAME* ctxframeptr = 0x1234; // Hypothetical address of the pointer.
/// Thread* threadAssociated = CONTAINING_RECORD(ctxframeptr, Thread, ctx); // Note that ctx is the member name for CTX_FRAME in the Thread struct.
#ifndef CONTAINING_RECORD
#define CONTAINING_RECORD(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#endif

#undef SIZE_T_MAX
#ifndef SIZE_T_MAX
#define SIZE_T_MAX (size_t)-1
#endif

#undef UINT64_T_MAX
#ifndef UINT64_T_MAX
#define UINT64_T_MAX (uint64_t)-1
#endif

#undef ARRAY_COUNTOF
#define ARRAY_COUNTOF(a) (sizeof(a) / sizeof((a)[0]))

extern uint8_t kernel_start;
#define LK_KERNEL_START &kernel_start

extern uint8_t kernel_end;
#define LK_KERNEL_END &kernel_end

#define LK_KERNEL_SIZE (LK_KERNEL_END - LK_KERNEL_START)

#ifndef _MSC_VER
#define MAX(a, b) ({ \
    __typeof__(a) _a = (a); \
    __typeof__(b) _b = (b); \
    _a > _b ? _a : _b; \
})

#define MIN(a, b) ({ \
    __typeof__(a) _a = (a); \
    __typeof__(b) _b = (b); \
    _a > _b ? _b : _a; \
})
#else
#define MAX(a,b) (0)
#define MIN(a,b) (0)
#endif

#if defined __GNUC__
#define RETADDR(level) __builtin_return_address(level)
#else
#define RETADDR(level) (void)(level)
#endif

// 'likely' hints to the compiler that the condition is expected to be true most of the time.
// It allows the compiler to optimize branch prediction.
#define likely(x)       __builtin_expect(!!(x), 1)

// 'unlikely' hints to the compiler that the condition is expected to be false most of the time.
// Useful for error handling or rare cases.
#define unlikely(x)     __builtin_expect(!!(x), 0)

#define FREEZE() __cli(); __hlt()

#define FREEZE_OTHER_CPUS()            \
    do {                              \
        IPI_PARAMS param = {0};       \
        MhSendActionToCpusAndWait(    \
            CPU_ACTION_STOP,          \
            param                    \
        );                            \
    } while (0)

#if !defined(__GNUC__)
#define FIELD_OFFSET(t,f)       ((uint32_t)(uint32_t*)&(((t*) 0)->f))
#else
#define FIELD_OFFSET(t,f)       ((uint32_t)__builtin_offsetof(t,f))
#endif

#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <stdbool.h>
#include "annotations.h"

/*
 * Portable fallback for MSVC and other C compilers.
 * The destination type is determined from sizeof(a).
 *
 * Valid for unsigned integer types up to 64 bits.
 */

FORCEINLINE
uint64_t
MeUnsignedMaximumForSize(
    size_t Size
)
{
    size_t BitCount;

    BitCount = Size * CHAR_BIT;

    if (BitCount >= 64)
        return UINT64_MAX;

    return (((uint64_t)1 << BitCount) - 1);
}

FORCEINLINE
bool
MeWillAddOverflowUnsigned(
    uint64_t A,
    uint64_t B,
    size_t DestinationSize
)
{
    uint64_t Maximum;

    if (DestinationSize == 0 ||
        DestinationSize > sizeof(uint64_t))
    {
        /*
         * Unsupported destination width.
         * Conservatively report overflow.
         */
        return true;
    }

    Maximum = MeUnsignedMaximumForSize(DestinationSize);

    /*
     * A/B may have originated from a wider unsigned type.
     */
    if (A > Maximum || B > Maximum)
        return true;

    return A > Maximum - B;
}

FORCEINLINE
bool
MeWillSubtractUnderflowUnsigned(
    uint64_t A,
    uint64_t B,
    size_t DestinationSize
)
{
    uint64_t Maximum;

    if (DestinationSize == 0 ||
        DestinationSize > sizeof(uint64_t))
    {
        return true;
    }

    Maximum = MeUnsignedMaximumForSize(DestinationSize);

    if (A > Maximum || B > Maximum)
        return true;

    return A < B;
}

/*
 * Visual Studio's IntelliSense parser only.
 *
 * Do not use _MSC_VER here, because that would disable the checks
 * in a real MSVC build.
 */
#if defined(__INTELLISENSE__)

#define WILL_ADD_OVERFLOW(a, b)          (0)
#define WILL_SUBTRACT_UNDERFLOW(a, b)    (0)

 /*
  * Native GCC/Clang C implementation.
  *
  * Exclude clang-cl because GNU statement expressions may not be
  * enabled when Clang is operating in MSVC compatibility mode.
  */
#elif (defined(__GNUC__) || defined(__clang__)) && !defined(_MSC_VER)

#define WILL_ADD_OVERFLOW(a, b)                                  \
    ({                                                           \
        __auto_type meOverflowA__ = (a);                         \
        __auto_type meOverflowB__ = (b);                         \
        __typeof__(meOverflowA__) meOverflowResult__;            \
        __builtin_add_overflow(                                  \
            meOverflowA__,                                      \
            meOverflowB__,                                      \
            &meOverflowResult__);                                \
    })

#define WILL_SUBTRACT_UNDERFLOW(a, b)                            \
    ({                                                           \
        __auto_type meUnderflowA__ = (a);                        \
        __auto_type meUnderflowB__ = (b);                        \
        __typeof__(meUnderflowA__) meUnderflowResult__;          \
        __builtin_sub_overflow(                                  \
            meUnderflowA__,                                     \
            meUnderflowB__,                                     \
            &meUnderflowResult__);                               \
    })

 /*
  * Actual MSVC, clang-cl, or another compiler.
  */
#else

#define WILL_ADD_OVERFLOW(a, b)                                  \
    MeWillAddOverflowUnsigned(                                   \
        (uint64_t)(a),                                           \
        (uint64_t)(b),                                           \
        sizeof(a))

#define WILL_SUBTRACT_UNDERFLOW(a, b)                            \
    MeWillSubtractUnderflowUnsigned(                             \
        (uint64_t)(a),                                           \
        (uint64_t)(b),                                           \
        sizeof(a))

#endif

// System V ABI Calling convention (Integer arguments)
// Argument 1: RDI
// Argument 2: RSI
// Argument 3: RDX
// Argument 4: RCX
// Argument 5: R8
// Argument 6: R9

#endif
