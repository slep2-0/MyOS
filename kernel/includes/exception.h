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

// ------------------ STRUCTURES ------------------

//struct _CONTEXT;

#define EXCEPTION_MAXIMUM_PARAMETERS 15
typedef struct _EXCEPTION_RECORD {
    MTSTATUS ExceptionCode;
    uint32_t ExceptionFlags;
    struct _EXCEPTION_RECORD* ExceptionRecord; // For nested exceptions
    void* ExceptionAddress;                     // RIP at time of fault
    //struct _CONTEXT ExceptionContext;
} EXCEPTION_RECORD, * PEXCEPTION_RECORD;

typedef enum _EXCEPTION_DISPOSITION {
    ExceptionContinueExecution = 0,
    ExceptionContinueSearch = 1,
    ExceptionNestedException = 2,
    ExceptionCollidedUnwind = 3
} EXCEPTION_DISPOSITION;

typedef struct _CONTEXT {
    uint64_t RFlags;
    uint64_t Dr0;
    uint64_t Dr1;
    uint64_t Dr2;
    uint64_t Dr3;
    uint64_t Dr6;
    uint64_t Dr7;
    uint64_t Rax;
    uint64_t Rcx;
    uint64_t Rdx;
    uint64_t Rbx;
    uint64_t Rsp;
    uint64_t Rbp;
    uint64_t Rsi;
    uint64_t Rdi;
    uint64_t R8;
    uint64_t R9;
    uint64_t R10;
    uint64_t R11;
    uint64_t R12;
    uint64_t R13;
    uint64_t R14;
    uint64_t R15;
    uint64_t Rip;
} CONTEXT, * PCONTEXT;

typedef struct _EXCEPTION_REGISTRATION_RECORD {
    struct _EXCEPTION_REGISTRATION_RECORD* Next;
    enum _EXCEPTION_DISPOSITION(*Handler)(struct _EXCEPTION_RECORD* arg1, void* Frame, struct _CONTEXT* arg2, void* DispCtx);
} EXCEPTION_REGISTRATION_RECORD;

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
extern bool ExpCaptureContext(IN PCONTEXT Context);

EXCEPTION_DISPOSITION MeStandardHandler(
    PEXCEPTION_RECORD ExceptionRecord,
    void* EstablisherFrame,
    PCONTEXT ContextRecord,
    void* DispatcherContext
);

// macros
#ifndef _MSC_VER
#define try do { \
    __label__ _try_start, _try_end, _except_label, _try_break;      \
    if (MeGetPreviousMode() == UserMode) __stac();                  \
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
    if (MeGetPreviousMode() == UserMode) __clac();      \
        /* The Page fault handler jumps here if we faulted */

#define end_try                                                     \
    }                                                               \
    _try_break:                                                     \
        if (MeGetPreviousMode() == UserMode) __clac();              \
} while (0)
#else
#define try 
#define except /* */
#define end_try
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

// raise.c
// unused func.
void
ExpRaiseStatus(
    IN MTSTATUS Status,
    IN uint64_t Rip
);

#endif