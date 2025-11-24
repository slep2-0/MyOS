#ifndef X86_MATANEL_MACROS_H
#define X86_MATANEL_MACROS_H
#include <stdint.h>

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

extern uint8_t kernel_start;
#define LK_KERNEL_START &kernel_start

extern uint8_t kernel_end;
#define LK_KERNEL_END &kernel_end

#define LK_KERNEL_SIZE (LK_KERNEL_END - LK_KERNEL_START)

#define MAX(a, b) (((a) > (b)) ? (a) : (b))

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

#endif