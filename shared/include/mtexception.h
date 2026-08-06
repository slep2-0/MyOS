#pragma once
#ifndef MATANELOS_SHARED_MTEXCEPTION_H
#define MATANELOS_SHARED_MTEXCEPTION_H

#include <stdint.h>
#include <stddef.h>

#define MT_EXCEPTION_MAXIMUM_PARAMETERS 15u

#define MT_CONTEXT_CONTROL         0x00000001u
#define MT_CONTEXT_INTEGER         0x00000002u
#define MT_CONTEXT_DEBUG_REGISTERS 0x00000004u
#define MT_CONTEXT_FULL \
    (MT_CONTEXT_CONTROL | MT_CONTEXT_INTEGER)

#define MT_EXCEPTION_NONCONTINUABLE 0x00000001u
#define MT_EXCEPTION_VALID_FLAGS \
    MT_EXCEPTION_NONCONTINUABLE

#define MT_EXCEPTION_CONTINUE_EXECUTION (-1)
#define MT_EXCEPTION_CONTINUE_SEARCH     0
#define MT_EXCEPTION_EXECUTE_HANDLER     1

#define MT_EXCEPTION_MAXIMUM_TRAVERSAL_COUNT 30

#define MT_HIGHEST_USER_ADDRESS ((uintptr_t)0x00007FFFFFFFFFFFULL)
#define MT_IS_CANONICAL_USER_ADDRESS(Address) \
    ((uintptr_t)(Address) <= MT_HIGHEST_USER_ADDRESS)

typedef struct _CONTEXT {
    uint32_t ContextFlags;
    uint32_t Reserved;

    uint64_t Rax, Rbx, Rcx, Rdx;
    uint64_t Rsi, Rdi, Rbp;
    uint64_t R8, R9, R10, R11;
    uint64_t R12, R13, R14, R15;

    uint64_t Rip;
    uint64_t Rsp;
    uint64_t RFlags;

    uint16_t SegCs;
    uint16_t SegSs;
    uint32_t Reserved2;

    uint64_t Dr0, Dr1, Dr2, Dr3, Dr6, Dr7;
} CONTEXT, * PCONTEXT;

typedef struct _EXCEPTION_RECORD {
    uint32_t ExceptionCode;
    uint32_t ExceptionFlags;
    struct _EXCEPTION_RECORD* ExceptionRecord;
    void* ExceptionAddress;
    uint32_t NumberParameters;
    uint32_t Reserved;
    uintptr_t ExceptionInformation[MT_EXCEPTION_MAXIMUM_PARAMETERS];
} EXCEPTION_RECORD, * PEXCEPTION_RECORD;

typedef struct _EXCEPTION_POINTERS {
    PEXCEPTION_RECORD ExceptionRecord;
    PCONTEXT ContextRecord;
} EXCEPTION_POINTERS, * PEXCEPTION_POINTERS;

typedef struct _EXCEPTION_DISPATCH_FRAME {
    EXCEPTION_RECORD ExceptionRecord;
    CONTEXT ContextRecord;
} EXCEPTION_DISPATCH_FRAME, * PEXCEPTION_DISPATCH_FRAME;

typedef enum _EXCEPTION_DISPOSITION {
    ExceptionContinueExecution = 0,
    ExceptionContinueSearch,
    ExceptionNestedException,
    ExceptionCollidedUnwind
} EXCEPTION_DISPOSITION;

typedef EXCEPTION_DISPOSITION(*PEXCEPTION_ROUTINE)(
    PEXCEPTION_RECORD ExceptionRecord,
    void* EstablisherFrame,
    PCONTEXT ContextRecord,
    void* DispatcherContext
    );

typedef struct _MT_LANGUAGE_CONTEXT {
    uint64_t Rbx;
    uint64_t Rbp;
    uint64_t R12;
    uint64_t R13;
    uint64_t R14;
    uint64_t R15;
    uint64_t Rsp;
    uint64_t Rip;
} MT_LANGUAGE_CONTEXT, * PMT_LANGUAGE_CONTEXT;

typedef enum _MT_LANGUAGE_ACTION {
    MtLanguageProtected = 0,
    MtLanguageEvaluateFilter,
    MtLanguageExecuteHandler
} MT_LANGUAGE_ACTION;

typedef struct _EXCEPTION_REGISTRATION_RECORD {
    struct _EXCEPTION_REGISTRATION_RECORD* Next;
    PEXCEPTION_ROUTINE Handler;
} EXCEPTION_REGISTRATION_RECORD, * PEXCEPTION_REGISTRATION_RECORD;

#define MT_EXCEPTION_CHAIN_END \
    ((PEXCEPTION_REGISTRATION_RECORD)(uintptr_t)-1)

typedef struct _MT_LANGUAGE_FRAME {
    EXCEPTION_REGISTRATION_RECORD Registration;
    MT_LANGUAGE_CONTEXT Continuation;

    EXCEPTION_RECORD ExceptionRecord;
    CONTEXT ContextRecord;
    EXCEPTION_POINTERS ExceptionPointers;

    PEXCEPTION_REGISTRATION_RECORD SearchNext;
    uint32_t Linked;
} MT_LANGUAGE_FRAME, * PMT_LANGUAGE_FRAME;

#endif
