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

#define EXCEPTION_MAXIMUM_PARAMETERS 15
typedef struct _EXCEPTION_RECORD {
    MTSTATUS ExceptionCode;
    uint32_t ExceptionFlags;
    struct _EXCEPTION_RECORD* ExceptionRecord; // For nested exceptions
    void* ExceptionAddress;                     // RIP at time of fault
    uint32_t NumberParameters;
    uintptr_t ExceptionInformation[EXCEPTION_MAXIMUM_PARAMETERS];
} EXCEPTION_RECORD, * PEXCEPTION_RECORD;

typedef enum _EXCEPTION_DISPOSITION {
    ExceptionContinueExecution = 0,
    ExceptionContinueSearch = 1,
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
    enum _EXCEPTION_DISPOSITION(*Handler)(struct _EXCEPTION_RECORD* arg1, struct _CONTEXT* arg2);
} EXCEPTION_REGISTRATION_RECORD;

typedef struct _EX_FRAME_REGISTRATION {
    PETHREAD Thread;
    EXCEPTION_REGISTRATION_RECORD* RegistrationPointer;
} EX_FRAME_REGISTRATION;

// ------------------ FUNCTIONS ------------------

// INTERNAL********
FORCEINLINE
void
__MeInternalCleanupFrame(EX_FRAME_REGISTRATION* pRegistration) {
    if (pRegistration->RegistrationPointer) {
        (pRegistration->RegistrationPointer->Handler) = NULL;
    }
}

extern PETHREAD PsGetCurrentThread(void);
extern bool ExpCaptureContext(IN PCONTEXT Context);

// macros
#define _try __UNIMPLEMENTED
#define _except __UNIMPLEMENTED

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

#endif