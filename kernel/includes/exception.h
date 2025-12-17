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
#define _try \
    { \
        PETHREAD _CurrentThread = PsGetCurrentThread(); \
        ME_EXCEPTION_FRAME _MyFrame; \
        _MyFrame.Next = (PME_EXCEPTION_FRAME)_CurrentThread->ExceptionList; \
        _MyFrame.Handler = MeStandardHandler; \
        _CurrentThread->ExceptionList = &_MyFrame; \
        /* Save Context. Returns 0 initially. Returns 1 if we crashed. */ \
        if (ExpCaptureContext(&_MyFrame) == 0) { \

#define _except(FilterExpression) \
            /* Success path: Unlink frame */ \
            _CurrentThread->ExceptionList = _MyFrame.Next; \
        } else { \
            /* Crash path: We just "landed" here from the handler! */ \
            /* Unlink frame (safe to do again) */ \
            _CurrentThread->ExceptionList = _MyFrame.Next; \
            /* You can access _MyFrame.ExceptionCode here if needed */ \
            { \
               /* User Code inside except block */

#define _end_except \
            } \
        } \
    }

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

bool
ExpIsPrivilegedInstruction(uint8_t* Ip /*, bool Wow64*/);

#endif