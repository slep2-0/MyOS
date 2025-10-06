/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:		Debugging Functions Prototypes and Definitions.
 */
#ifndef X86_DEBUG_FUNCS_H
#define X86_DEBUG_FUNCS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../mtstatus.h"
#include "../intrinsics/intrin.h"
#include "../cpu/cpu_types.h"

#ifndef FORCEINLINE
#if defined(__clang__) || defined(__GNUC__)
#define FORCEINLINE static inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define FORCEINLINE static __forceinline
#else
#define FORCEINLINE static inline
#endif
#endif

typedef enum _DEBUG_ACCESS_MODE {
    DEBUG_ACCESS_EXECUTE = 0b00,    // Break on instruction execution
    DEBUG_ACCESS_WRITE = 0b01,    // Break on data writes
    DEBUG_ACCESS_IO = 0b10,    // Break on I/O read or write (legacy)
    DEBUG_ACCESS_READWRITE = 0b11   // Break on data reads or writes
} DEBUG_ACCESS_MODE;

typedef enum _DEBUG_LENGTH {
    DEBUG_LEN_1 = 0b00,
    DEBUG_LEN_2 = 0b01,
    DEBUG_LEN_8 = 0b10, // Only valid in long mode
    DEBUG_LEN_4 = 0b11
} DEBUG_LENGTH;

typedef struct _DBG_CALLBACK_INFO {
    void* Address;           /* breakpoint address (DRx) */
    CTX_FRAME* CpuCtx;       /* general CPU context / registers */
    INT_FRAME* IntFrame;     /* interrupt frame */
    int   BreakIdx;         /* which DRx (0..3) fired */
    uint64_t Dr6;           /* raw DR6 value at time of trap */
} DBG_CALLBACK_INFO;

MTSTATUS MtSetHardwareBreakpoint(DebugCallback CallbackFunction, void* BreakpointAddress, DEBUG_ACCESS_MODE AccessMode, DEBUG_LENGTH Length);
MTSTATUS MtClearHardwareBreakpointByIndex(int index);
MTSTATUS MtClearHardwareBreakpointByAddress(void* BreakpointAddress);
/* Find a free debug slot (0..3) or -1 if none */
int find_available_debug_reg(void);
/// <summary>
/// Returns the current function return address pointer (for monitoring)
/// Usage: MtSetHardwareBreakpoint(callback, MtGetFunctionRipAddress(), accessMode, length)
/// This function is inline, which means it will not return 'this' function ret addr, but the function this inline function is being executed on.
/// (THIS DOES NOT RETURN THE RETURN ADDRESS ITSELF, ONLY STACK SLOT THAT HOLDS IT, THAT IS USED FOR MONITORING - TO GET RET ADDR - USE A __builtin_)
/// </summary>
/// <returns>Pointer to the slot of the ret addr in the stack ([rbp+1])</returns>
FORCEINLINE void* MtGetFunctionRipAddress(void) {
    void** frame = (void**)__builtin_frame_address(0); // returns RBP
    return (void*)(frame + 1);  // address of saved RIP slot
}
#endif
