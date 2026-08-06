#ifndef X86_MATANEL_EXCEPTION
#define X86_MATANEL_EXCEPTION

/*++

Module Name:

	exception.h

Purpose:

	This module contains the header files & prototypes required for runtime exception handling of the OS.

Author:

	slep (Matanel) 2025.

Revision History:

--*/

// Base includes
#include <stdint.h>
#include <stddef.h>

// Other file includes
#include "me.h"
#include "core.h"
#include "../../shared/include/mtexception.h"

// ------------------ STRUCTURES ------------------

typedef struct _EX_FRAME_REGISTRATION {
    PETHREAD Thread;
    EXCEPTION_REGISTRATION_RECORD* RegistrationPointer;
} EX_FRAME_REGISTRATION;

typedef struct _EXCEPTION_RANGE {
    uint64_t start_addr;
    uint64_t end_addr;
    uint64_t handler_addr;
} EXCEPTION_RANGE, *PEXCEPTION_RANGE;

// Symbols defined by the linker script
extern EXCEPTION_RANGE __start_ex_table[];
extern EXCEPTION_RANGE __stop_ex_table[];

// Helper to search the table
uint64_t MiSearchExceptionTable(uint64_t rip);

// ------------------ FUNCTIONS ------------------

extern PETHREAD PsGetCurrentThread(void);

void
ExpCaptureContextFromTrapFrame(
    IN const TRAP_FRAME* TrapFrame,
    OUT PCONTEXT Context
);

// user context only
MTSTATUS
ExpApplyUserContextToTrapFrame(
    IN const CONTEXT* Context,
    IN OUT PTRAP_FRAME TrapFrame
);

void
ExpInitializeExceptionRecord(
    MTSTATUS Status,
    const TRAP_FRAME* TrapFrame,
    PEXCEPTION_RECORD ExceptionRecord
);

void
ExpInitializeAccessViolationRecord(
    MTSTATUS Status,
    const TRAP_FRAME* TrapFrame,
    uint64_t FaultAddress,
    uint64_t PageFaultErrorCode,
    PEXCEPTION_RECORD ExceptionRecord
);

MTSTATUS
ExpPublishUserException(
    IN const EXCEPTION_RECORD* ExceptionRecord
);

MTSTATUS
ExpPrepareUserExceptionDispatch(
    IN OUT PTRAP_FRAME TrapFrame
);

#ifdef DEBUG
void
ExpTestContextConversion(
    void
);

void
ExpTestExceptionRecordConstruction(
    void
);

void
ExpTestUserExceptionPublication(
    void
);

void
ExpTestUserExceptionDispatchFrame(
    IN PETHREAD TargetThread
);
#endif

// macros
// Try except blocks mean we will most likely touch user accessible memory, so stac and clac are always included no matter the previousmode.
#ifndef _MSC_VER
#define try do { \
    __label__ _try_start, _try_end, _except_label, _try_break;      \
    __stac();                                                       \
    /*  Emit the table entry linking this range to the handler */   \
    __asm__ volatile (                                              \
        ".section __ex_table,\"a\"\n\t"                             \
        ".quad %P0, %P1, %P2\n\t"                                   \
        ".previous\n\t"                                             \
            :                                                       \
        : "i" (&& _try_start), "i" (&& _try_end), "i" (&& _except_label)\
    );                                                          \
    /* Start of protected region */                                 \
    _try_start:                                                     \
    __asm__ volatile("" ::: "memory"); /* Prevent hoisting */       \
    {

#define except                                          \
    }                                                   \
    __asm__ volatile("" ::: "memory");                  \
    _try_end:                                          \
    {                                                   \
        int _volatile_true = 1;                         \
        __asm__ volatile("" : "+r"(_volatile_true));    \
        if (_volatile_true) goto _try_break;            \
    }                                                   \
    _except_label:                                      \
    {                                                   \
    __clac();                                           \
        /* The Page fault handler jumps here if we faulted */

#define end_try                                                     \
    }                                                               \
    _try_break:                                                     \
        __clac();                                                   \
} while (0)
#define leave   do { goto _try_break; } while (0)
#else
#define try 
#define except /* */
#define end_try
#define leave
#endif
bool
ExpIsExceptionHandlerPresent(
    IN PETHREAD Thread
);

void
ExpDispatchException(
    IN PTRAP_FRAME TrapFrame,
    IN PCONTEXT ContextRecord,
    IN PEXCEPTION_RECORD ExceptionRecord
);

uint64_t
ExpFindKernelModeExceptionHandler(
    uint64_t Rip
);

// instruction.c

bool
ExpIsPrivilegedInstruction(uint8_t* Ip /*, bool Wow64*/);

// probe.c

MTSTATUS
ProbeForRead(
    IN const void* Address,
    IN size_t Length,
    IN uint32_t Alignment
);

NORETURN
void
ExpRaiseStatus(
    IN MTSTATUS Status
);

FORCEINLINE
PRIVILEGE_MODE
ExpGetFaultMode(
    IN const TRAP_FRAME* TrapFrame
)

{
    return ((TrapFrame->cs & 0x3u) == 0x3u) ? UserMode : KernelMode;
}

#endif
