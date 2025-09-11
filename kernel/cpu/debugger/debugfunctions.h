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
#include "../../mtstatus.h"
#include "../../intrin/intrin.h"
#include "../../cpu/cpu_types.h"

typedef void (*DebugCallback)(void*);

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

typedef struct _DEBUG_ENTRY {
    void* Address;
    DebugCallback Callback;
} DEBUG_ENTRY;

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

#endif
