#include "../includes/mtdll.h"
#include "exception_chain_test.h"

#ifndef MATANELOS_EXCEPTION_CHAIN_TEST
#error "exception_chain.c is test-only and must not enter an ordinary MTDLL build"
#endif

#define MTP_EXCEPTION_TEST_TIMEOUT_MS 5000U
#define MTP_EXCEPTION_TEST_FAILURE_BASE ((MTSTATUS)0xC8060000L)
#define MTP_NATIVE_RAISE_CODE ((MTSTATUS)0xE8060100L)
#define MTP_NATIVE_RAISE_INFORMATION ((uintptr_t)0x1122334455667788ULL)
#define MTP_NATIVE_RESUME_STATUS ((MTSTATUS)0x00006123L)
#define MTP_LANGUAGE_CONTINUE_CODE ((MTSTATUS)0xE8060200L)
#define MTP_LANGUAGE_EXECUTE_CODE ((MTSTATUS)0xE8060201L)
#define MTP_LANGUAGE_SEARCH_CODE ((MTSTATUS)0xE8060202L)
#define MTP_LANGUAGE_RESUME_STATUS ((MTSTATUS)0x00006234L)
#define MTP_ACCESS_RESUME_STATUS ((MTSTATUS)0x00006400L)
#define MTP_ACCESS_TEST_REGION_SIZE 4096U
#define MTP_DIVIDE_RESUME_STATUS ((MTSTATUS)0x00006501L)
#define MTP_INVALID_OPCODE_RESUME_STATUS ((MTSTATUS)0x00006502L)
#define MTP_PRIVILEGED_RESUME_STATUS ((MTSTATUS)0x00006503L)
#define MTP_GENERAL_PROTECTION_RESUME_STATUS ((MTSTATUS)0x00006504L)
#define MTP_NONCANONICAL_ADDRESS ((void*)(uintptr_t)0x0000800000000000ULL)

MTDLL_API void
MtpExceptionStressApc(
    IN void* NormalContext,
    IN void* SystemArgument1,
    IN void* SystemArgument2
)
{
    (void)NormalContext;
    (void)SystemArgument1;
    (void)SystemArgument2;
}

typedef enum _MTP_EXCEPTION_FATAL_CASE {
    MtpFatalRecordOutsideStack = 1,
    MtpFatalInvalidHandler,
    MtpFatalInvalidNext,
    MtpFatalSelfCycle,
    MtpFatalExcessiveLength,
    MtpFatalInvalidDisposition,
    MtpFatalNestedDisposition,
    MtpFatalCollidedUnwind,
    MtpFatalNoncontinuableContinuation
} MTP_EXCEPTION_FATAL_CASE;

typedef struct _MTP_EXCEPTION_VISIT_CONTEXT {
    PEXCEPTION_REGISTRATION_RECORD Frames;
    size_t FrameCount;
    size_t VisitCount;
    size_t MutationIndex;
    uint64_t MutationValue;
    bool Failed;
} MTP_EXCEPTION_VISIT_CONTEXT;

typedef struct _MTP_NATIVE_RAISE_CONTEXT {
    PEXCEPTION_REGISTRATION_RECORD ExpectedFrame;
    void* ExpectedAddress;
    volatile uint32_t VisitCount;
    volatile bool Failed;
} MTP_NATIVE_RAISE_CONTEXT;

typedef struct _MTP_LANGUAGE_SEARCH_CONTEXT {
    PMT_LANGUAGE_FRAME OuterFrame;
    volatile uint32_t InnerFilterCount;
    volatile uint32_t OuterFilterCount;
    volatile uint32_t Failure;
} MTP_LANGUAGE_SEARCH_CONTEXT;

typedef struct _MTP_ACCESS_VIOLATION_EXPECTATION {
    void* FaultAddress;
    void* InstructionAddress;
    uintptr_t Operation;
    uintptr_t ResumeAddress;
    volatile uint32_t FilterCount;
    volatile uint32_t Failure;
    bool ContinueExecution;
} MTP_ACCESS_VIOLATION_EXPECTATION;

typedef struct _MTP_PROCESSOR_EXCEPTION_EXPECTATION {
    MTSTATUS ExceptionCode;
    void* InstructionAddress;
    uintptr_t ResumeAddress;
    volatile uint32_t FilterCount;
    volatile uint32_t Failure;
} MTP_PROCESSOR_EXCEPTION_EXPECTATION;

_Static_assert(offsetof(MT_LANGUAGE_CONTEXT, Rbx) == 0x00,
    "language context RBX offset changed");
_Static_assert(offsetof(MT_LANGUAGE_CONTEXT, Rbp) == 0x08,
    "language context RBP offset changed");
_Static_assert(offsetof(MT_LANGUAGE_CONTEXT, R12) == 0x10,
    "language context R12 offset changed");
_Static_assert(offsetof(MT_LANGUAGE_CONTEXT, R13) == 0x18,
    "language context R13 offset changed");
_Static_assert(offsetof(MT_LANGUAGE_CONTEXT, R14) == 0x20,
    "language context R14 offset changed");
_Static_assert(offsetof(MT_LANGUAGE_CONTEXT, R15) == 0x28,
    "language context R15 offset changed");
_Static_assert(offsetof(MT_LANGUAGE_CONTEXT, Rsp) == 0x30,
    "language context RSP offset changed");
_Static_assert(offsetof(MT_LANGUAGE_CONTEXT, Rip) == 0x38,
    "language context RIP offset changed");
_Static_assert(sizeof(MT_LANGUAGE_CONTEXT) == 0x40,
    "language context size changed");

extern uint32_t
MtpRunLanguageContextAssemblyTest(
    void
);

extern MTSTATUS
MtpCauseReadAccessViolation(
    const volatile void* Address
);

extern uint8_t MtpReadAccessViolationInstruction;
extern uint8_t MtpReadAccessViolationResume;

extern MTSTATUS MtpCauseIntegerDivideByZero(void);
extern uint8_t MtpIntegerDivideInstruction;
extern uint8_t MtpIntegerDivideResume;

extern MTSTATUS MtpCauseInvalidOpcode(void);
extern uint8_t MtpInvalidOpcodeInstruction;
extern uint8_t MtpInvalidOpcodeResume;

extern MTSTATUS MtpCausePrivilegedInstruction(void);
extern uint8_t MtpPrivilegedInstruction;
extern uint8_t MtpPrivilegedInstructionResume;

extern MTSTATUS MtpCauseGeneralProtectionAccess(const volatile void* Address);
extern uint8_t MtpGeneralProtectionAccessInstruction;
extern uint8_t MtpGeneralProtectionAccessResume;

static MTSTATUS
MtpExceptionTestFailure(
    uint32_t Test
)
{
    return MTP_EXCEPTION_TEST_FAILURE_BASE | (MTSTATUS)Test;
}

static int
MtpAccessViolationFilter(
    PEXCEPTION_POINTERS Information,
    MTP_ACCESS_VIOLATION_EXPECTATION* Expectation
)
{
    Expectation->FilterCount++;

    if (!Information || !Information->ExceptionRecord ||
        !Information->ContextRecord) {
        Expectation->Failure = 1;
    }
    else if (Information->ExceptionRecord->ExceptionCode !=
        (uint32_t)MT_ACCESS_VIOLATION) {
        Expectation->Failure = 2;
    }
    else if (Information->ExceptionRecord->NumberParameters != 2) {
        Expectation->Failure = 3;
    }
    else if (Information->ExceptionRecord->ExceptionInformation[0] !=
        Expectation->Operation) {
        Expectation->Failure = 4;
    }
    else if (Information->ExceptionRecord->ExceptionInformation[1] !=
        (uintptr_t)Expectation->FaultAddress) {
        Expectation->Failure = 5;
    }
    else if (Expectation->InstructionAddress &&
        Information->ExceptionRecord->ExceptionAddress !=
            Expectation->InstructionAddress) {
        Expectation->Failure = 6;
    }

    if (Expectation->Failure != 0 || !Expectation->ContinueExecution) {
        return MT_EXCEPTION_EXECUTE_HANDLER;
    }

    Information->ContextRecord->Rip = Expectation->ResumeAddress;
    return MT_EXCEPTION_CONTINUE_EXECUTION;
}

static uint32_t
MtpUnhandledAccessViolationWorker(
    void* Parameter
)
{
    volatile uint8_t Value = *(volatile uint8_t*)Parameter;
    (void)Value;
    return (uint32_t)MtpExceptionTestFailure(239);
}

static int
MtpProcessorExceptionFilter(
    PEXCEPTION_POINTERS Information,
    MTP_PROCESSOR_EXCEPTION_EXPECTATION* Expectation
)
{
    Expectation->FilterCount++;

    if (!Information || !Information->ExceptionRecord ||
        !Information->ContextRecord) {
        Expectation->Failure = 1;
    }
    else if (Information->ExceptionRecord->ExceptionCode !=
        (uint32_t)Expectation->ExceptionCode) {
        Expectation->Failure = 2;
    }
    else if (Information->ExceptionRecord->ExceptionFlags != 0) {
        Expectation->Failure = 3;
    }
    else if (Information->ExceptionRecord->NumberParameters != 0) {
        Expectation->Failure = 4;
    }
    else if (Information->ExceptionRecord->ExceptionAddress !=
        Expectation->InstructionAddress) {
        Expectation->Failure = 5;
    }

    if (Expectation->Failure != 0) {
        return MT_EXCEPTION_EXECUTE_HANDLER;
    }

    Information->ContextRecord->Rip = Expectation->ResumeAddress;
    return MT_EXCEPTION_CONTINUE_EXECUTION;
}

static uint32_t
MtpUnhandledDivideWorker(
    void* Parameter
)
{
    (void)Parameter;
    (void)MtpCauseIntegerDivideByZero();
    return (uint32_t)MtpExceptionTestFailure(280);
}

static uint32_t
MtpUnhandledInvalidOpcodeWorker(
    void* Parameter
)
{
    (void)Parameter;
    (void)MtpCauseInvalidOpcode();
    return (uint32_t)MtpExceptionTestFailure(281);
}

static uint32_t
MtpUnhandledPrivilegedWorker(
    void* Parameter
)
{
    (void)Parameter;
    (void)MtpCausePrivilegedInstruction();
    return (uint32_t)MtpExceptionTestFailure(282);
}

static uint32_t
MtpUnhandledGeneralProtectionWorker(
    void* Parameter
)
{
    (void)Parameter;
    (void)MtpCauseGeneralProtectionAccess(MTP_NONCANONICAL_ADDRESS);
    return (uint32_t)MtpExceptionTestFailure(283);
}

static MTSTATUS
MtpTestRealProcessorException(
    MTSTATUS (*CauseException)(void),
    MTSTATUS ExpectedResumeStatus,
    MTSTATUS ExpectedExceptionCode,
    void* InstructionAddress,
    uintptr_t ResumeAddress,
    uint32_t FailureBase
)
{
    MTP_PROCESSOR_EXCEPTION_EXPECTATION Expectation = {
        .ExceptionCode = ExpectedExceptionCode,
        .InstructionAddress = InstructionAddress,
        .ResumeAddress = ResumeAddress
    };
    volatile uint32_t UnexpectedHandler = 0;
    MTSTATUS ResumeStatus = MT_SUCCESS;

    __try {
        ResumeStatus = CauseException();
    }
    __except (
        MtpProcessorExceptionFilter(
            GetExceptionInformation(),
            &Expectation
        )
    ) {
        UnexpectedHandler = 1;
    }

    if (UnexpectedHandler || ResumeStatus != ExpectedResumeStatus ||
        Expectation.FilterCount != 1 || Expectation.Failure != 0) {
        return MtpExceptionTestFailure(FailureBase + Expectation.Failure);
    }

    return MT_SUCCESS;
}

static MTSTATUS
MtpTestRealGeneralProtectionAccess(
    void
)
{
    MTP_PROCESSOR_EXCEPTION_EXPECTATION Expectation = {
        .ExceptionCode = MT_ACCESS_VIOLATION,
        .InstructionAddress = &MtpGeneralProtectionAccessInstruction,
        .ResumeAddress = (uintptr_t)&MtpGeneralProtectionAccessResume
    };
    volatile uint32_t UnexpectedHandler = 0;
    MTSTATUS ResumeStatus = MT_SUCCESS;

    __try {
        ResumeStatus = MtpCauseGeneralProtectionAccess(
            MTP_NONCANONICAL_ADDRESS
        );
    }
    __except (
        MtpProcessorExceptionFilter(
            GetExceptionInformation(),
            &Expectation
        )
    ) {
        UnexpectedHandler = 1;
    }

    if (UnexpectedHandler ||
        ResumeStatus != MTP_GENERAL_PROTECTION_RESUME_STATUS ||
        Expectation.FilterCount != 1 || Expectation.Failure != 0) {
        return MtpExceptionTestFailure(275 + Expectation.Failure);
    }

    return MT_SUCCESS;
}

static MTSTATUS
MtpTestRealProcessorExceptions(
    void
)
{
    MTSTATUS Status = MtpTestRealProcessorException(
        MtpCauseIntegerDivideByZero,
        MTP_DIVIDE_RESUME_STATUS,
        MT_INTEGER_DIVIDE_BY_ZERO,
        &MtpIntegerDivideInstruction,
        (uintptr_t)&MtpIntegerDivideResume,
        260
    );
    if (MT_FAILURE(Status)) return Status;

    Status = MtpTestRealProcessorException(
        MtpCauseInvalidOpcode,
        MTP_INVALID_OPCODE_RESUME_STATUS,
        MT_ILLEGAL_INSTRUCTION,
        &MtpInvalidOpcodeInstruction,
        (uintptr_t)&MtpInvalidOpcodeResume,
        265
    );
    if (MT_FAILURE(Status)) return Status;

    Status = MtpTestRealProcessorException(
        MtpCausePrivilegedInstruction,
        MTP_PRIVILEGED_RESUME_STATUS,
        MT_PRIVILEGED_INSTRUCTION,
        &MtpPrivilegedInstruction,
        (uintptr_t)&MtpPrivilegedInstructionResume,
        270
    );
    if (MT_FAILURE(Status)) return Status;

    return MtpTestRealGeneralProtectionAccess();
}

static MTSTATUS
MtpTestUnhandledProcessorException(
    uint32_t (*Worker)(void*),
    MTSTATUS ExpectedStatus,
    uint32_t FailureBase
)
{
    HANDLE Thread = CreateThread(Worker, NULL);
    if (Thread == MT_INVALID_HANDLE) {
        return MtpExceptionTestFailure(FailureBase);
    }

    uint32_t WaitResult = WaitForSingleObject(
        Thread,
        MTP_EXCEPTION_TEST_TIMEOUT_MS
    );
    bool Waited = WaitResult == WAIT_OBJECT_0;
    if (!Waited) {
        TerminateThread(Thread, MT_INVALID_CHECK);
        (void)WaitForSingleObject(Thread, MTP_EXCEPTION_TEST_TIMEOUT_MS);
    }

    uint32_t ExitCode = 0;
    bool Queried = Waited && GetExitCodeThread(Thread, &ExitCode);
    bool Closed = CloseHandle(Thread);

    if (!Waited || !Queried || !Closed ||
        ExitCode != (uint32_t)ExpectedStatus) {
        return MtpExceptionTestFailure(FailureBase + 1);
    }

    return MT_SUCCESS;
}

static MTSTATUS
MtpTestUnhandledProcessorExceptions(
    void
)
{
    MTSTATUS Status = MtpTestUnhandledProcessorException(
        MtpUnhandledDivideWorker,
        MT_INTEGER_DIVIDE_BY_ZERO,
        284
    );
    if (MT_FAILURE(Status)) return Status;

    Status = MtpTestUnhandledProcessorException(
        MtpUnhandledInvalidOpcodeWorker,
        MT_ILLEGAL_INSTRUCTION,
        286
    );
    if (MT_FAILURE(Status)) return Status;

    Status = MtpTestUnhandledProcessorException(
        MtpUnhandledPrivilegedWorker,
        MT_PRIVILEGED_INSTRUCTION,
        288
    );
    if (MT_FAILURE(Status)) return Status;

    return MtpTestUnhandledProcessorException(
        MtpUnhandledGeneralProtectionWorker,
        MT_ACCESS_VIOLATION,
        290
    );
}

static EXCEPTION_DISPOSITION
MtpFaultingExceptionHandler(
    PEXCEPTION_RECORD ExceptionRecord,
    void* EstablisherFrame,
    PCONTEXT ContextRecord,
    void* DispatcherContext
)
{
    (void)ExceptionRecord;
    (void)EstablisherFrame;
    (void)ContextRecord;
    (void)DispatcherContext;

    // Fault while the kernel still owns the active user-exception dispatch.
    (void)MtpCauseReadAccessViolation(NULL);
    return ExceptionContinueSearch;
}

static uint32_t
MtpHandlerFaultWorker(
    void* Parameter
)
{
    (void)Parameter;

    EXCEPTION_REGISTRATION_RECORD Frame = { 0 };
    EXCEPTION_RECORD Record = {
        .ExceptionCode = (uint32_t)MTP_NATIVE_RAISE_CODE
    };

    MtpPushExceptionFrame(&Frame, MtpFaultingExceptionHandler);
    (void)MtRaiseException(&Record);
    return (uint32_t)MtpExceptionTestFailure(292);
}

static MTSTATUS
MtpTestHandlerFault(
    void
)
{
    return MtpTestUnhandledProcessorException(
        MtpHandlerFaultWorker,
        MT_INVALID_STATE,
        293
    );
}

static MTSTATUS
MtpTestRealAccessViolations(
    void
)
{
    volatile uint32_t UnexpectedHandler = 0;
    MTSTATUS Result = MT_SUCCESS;

    void* NoAccess = VirtualAlloc(
        NULL,
        MTP_ACCESS_TEST_REGION_SIZE,
        PAGE_NOACCESS
    );
    if (!NoAccess) {
        return MtpExceptionTestFailure(220);
    }

    MTP_ACCESS_VIOLATION_EXPECTATION ReadExpectation = {
        .FaultAddress = NoAccess,
        .InstructionAddress = &MtpReadAccessViolationInstruction,
        .Operation = 0,
        .ResumeAddress = (uintptr_t)&MtpReadAccessViolationResume,
        .ContinueExecution = true
    };

    __try {
        if (MtCurrentTeb()->MtTib.ExceptionList ==
            MT_EXCEPTION_CHAIN_END) {
            Result = MtpExceptionTestFailure(294);
        }
        else {
            MTSTATUS ResumeStatus = MtpCauseReadAccessViolation(NoAccess);
            if (ResumeStatus != MTP_ACCESS_RESUME_STATUS) {
                Result = MtpExceptionTestFailure(221);
            }
        }
    }
    __except (
        MtpAccessViolationFilter(
            GetExceptionInformation(),
            &ReadExpectation
        )
    ) {
        UnexpectedHandler = 1;
    }

    if (UnexpectedHandler || ReadExpectation.FilterCount != 1 ||
        ReadExpectation.Failure != 0 || MT_FAILURE(Result)) {
        Result = MtpExceptionTestFailure(
            222 + ReadExpectation.Failure
        );
    }

    if (!VirtualFree(NoAccess, 0, MEM_RELEASE) && MT_SUCCEEDED(Result)) {
        Result = MtpExceptionTestFailure(229);
    }
    if (MT_FAILURE(Result)) {
        return Result;
    }

    void* ReadOnly = VirtualAlloc(
        NULL,
        MTP_ACCESS_TEST_REGION_SIZE,
        PAGE_READONLY
    );
    if (!ReadOnly) {
        return MtpExceptionTestFailure(230);
    }

    MTP_ACCESS_VIOLATION_EXPECTATION WriteExpectation = {
        .FaultAddress = ReadOnly,
        .Operation = 1
    };
    volatile uint32_t WriteHandled = 0;
    volatile uint32_t WriteContinued = 0;

    __try {
        *(volatile uint8_t*)ReadOnly = 0xA5;
        WriteContinued = 1;
    }
    __except (
        MtpAccessViolationFilter(
            GetExceptionInformation(),
            &WriteExpectation
        )
    ) {
        WriteHandled = 1;
    }

    if (!VirtualFree(ReadOnly, 0, MEM_RELEASE)) {
        return MtpExceptionTestFailure(231);
    }
    if (WriteContinued || !WriteHandled ||
        WriteExpectation.FilterCount != 1 ||
        WriteExpectation.Failure != 0) {
        return MtpExceptionTestFailure(232 + WriteExpectation.Failure);
    }

    void* NonExecutable = VirtualAlloc(
        NULL,
        MTP_ACCESS_TEST_REGION_SIZE,
        PAGE_READWRITE
    );
    if (!NonExecutable) {
        return MtpExceptionTestFailure(240);
    }
    *(volatile uint8_t*)NonExecutable = 0xC3;

    MTP_ACCESS_VIOLATION_EXPECTATION ExecuteExpectation = {
        .FaultAddress = NonExecutable,
        .InstructionAddress = NonExecutable,
        .Operation = 8
    };
    volatile uint32_t ExecuteHandled = 0;
    volatile uint32_t ExecuteContinued = 0;

    __try {
        ((void (*)(void))NonExecutable)();
        ExecuteContinued = 1;
    }
    __except (
        MtpAccessViolationFilter(
            GetExceptionInformation(),
            &ExecuteExpectation
        )
    ) {
        ExecuteHandled = 1;
    }

    if (!VirtualFree(NonExecutable, 0, MEM_RELEASE)) {
        return MtpExceptionTestFailure(241);
    }
    if (ExecuteContinued || !ExecuteHandled ||
        ExecuteExpectation.FilterCount != 1 ||
        ExecuteExpectation.Failure != 0) {
        return MtpExceptionTestFailure(242 + ExecuteExpectation.Failure);
    }

    return MT_SUCCESS;
}

static MTSTATUS
MtpTestUnhandledAccessViolation(
    void
)
{
    void* UnhandledAddress = VirtualAlloc(
        NULL,
        MTP_ACCESS_TEST_REGION_SIZE,
        PAGE_NOACCESS
    );
    if (!UnhandledAddress) {
        return MtpExceptionTestFailure(250);
    }

    HANDLE Thread = CreateThread(
        MtpUnhandledAccessViolationWorker,
        UnhandledAddress
    );
    if (Thread == MT_INVALID_HANDLE) {
        VirtualFree(UnhandledAddress, 0, MEM_RELEASE);
        return MtpExceptionTestFailure(251);
    }

    uint32_t WaitResult = WaitForSingleObject(
        Thread,
        MTP_EXCEPTION_TEST_TIMEOUT_MS
    );
    bool Waited = WaitResult == WAIT_OBJECT_0;
    if (!Waited) {
        TerminateThread(Thread, MT_INVALID_CHECK);
        (void)WaitForSingleObject(Thread, MTP_EXCEPTION_TEST_TIMEOUT_MS);
    }

    uint32_t ExitCode = 0;
    bool Queried = Waited && GetExitCodeThread(Thread, &ExitCode);
    bool Closed = CloseHandle(Thread);
    bool Freed = VirtualFree(UnhandledAddress, 0, MEM_RELEASE);

    if (!Waited || !Queried || !Closed || !Freed ||
        ExitCode != (uint32_t)MT_ACCESS_VIOLATION) {
        return MtpExceptionTestFailure(252);
    }

    return MT_SUCCESS;
}

static EXCEPTION_DISPOSITION
MtpVisitHandler(
    PEXCEPTION_RECORD ExceptionRecord,
    void* EstablisherFrame,
    PCONTEXT ContextRecord,
    void* DispatcherContext
)
{
    (void)DispatcherContext;

    MTP_EXCEPTION_VISIT_CONTEXT* Test =
        (MTP_EXCEPTION_VISIT_CONTEXT*)ExceptionRecord->ExceptionInformation[0];
    PEXCEPTION_REGISTRATION_RECORD Frame = EstablisherFrame;
    size_t Index = (size_t)(Frame - Test->Frames);

    if (Index >= Test->FrameCount || Index != Test->VisitCount) {
        Test->Failed = true;
        return ExceptionContinueExecution;
    }

    Test->VisitCount++;
    if (Index == Test->MutationIndex) {
        ContextRecord->Rax = Test->MutationValue;
    }

    return Test->VisitCount == Test->FrameCount
        ? ExceptionContinueExecution
        : ExceptionContinueSearch;
}

static EXCEPTION_DISPOSITION
MtpContinueSearchHandler(
    PEXCEPTION_RECORD ExceptionRecord,
    void* EstablisherFrame,
    PCONTEXT ContextRecord,
    void* DispatcherContext
)
{
    (void)ExceptionRecord;
    (void)EstablisherFrame;
    (void)ContextRecord;
    (void)DispatcherContext;
    return ExceptionContinueSearch;
}

static EXCEPTION_DISPOSITION
MtpContinueExecutionHandler(
    PEXCEPTION_RECORD ExceptionRecord,
    void* EstablisherFrame,
    PCONTEXT ContextRecord,
    void* DispatcherContext
)
{
    (void)ExceptionRecord;
    (void)EstablisherFrame;
    (void)ContextRecord;
    (void)DispatcherContext;
    return ExceptionContinueExecution;
}

static EXCEPTION_DISPOSITION
MtpInvalidDispositionHandler(
    PEXCEPTION_RECORD ExceptionRecord,
    void* EstablisherFrame,
    PCONTEXT ContextRecord,
    void* DispatcherContext
)
{
    (void)ExceptionRecord;
    (void)EstablisherFrame;
    (void)ContextRecord;
    (void)DispatcherContext;
    return (EXCEPTION_DISPOSITION)UINT32_MAX;
}

static EXCEPTION_DISPOSITION
MtpNestedDispositionHandler(
    PEXCEPTION_RECORD ExceptionRecord,
    void* EstablisherFrame,
    PCONTEXT ContextRecord,
    void* DispatcherContext
)
{
    (void)ExceptionRecord;
    (void)EstablisherFrame;
    (void)ContextRecord;
    (void)DispatcherContext;
    return ExceptionNestedException;
}

static EXCEPTION_DISPOSITION
MtpCollidedUnwindHandler(
    PEXCEPTION_RECORD ExceptionRecord,
    void* EstablisherFrame,
    PCONTEXT ContextRecord,
    void* DispatcherContext
)
{
    (void)ExceptionRecord;
    (void)EstablisherFrame;
    (void)ContextRecord;
    (void)DispatcherContext;
    return ExceptionCollidedUnwind;
}

static EXCEPTION_DISPOSITION
MtpMutateNextHandler(
    PEXCEPTION_RECORD ExceptionRecord,
    void* EstablisherFrame,
    PCONTEXT ContextRecord,
    void* DispatcherContext
)
{
    (void)ContextRecord;
    (void)DispatcherContext;

    size_t* VisitCount =
        (size_t*)ExceptionRecord->ExceptionInformation[0];
    (*VisitCount)++;

    PEXCEPTION_REGISTRATION_RECORD Frame = EstablisherFrame;
    Frame->Next = MT_EXCEPTION_CHAIN_END;
    return ExceptionContinueSearch;
}

static EXCEPTION_DISPOSITION
MtpSavedNextTailHandler(
    PEXCEPTION_RECORD ExceptionRecord,
    void* EstablisherFrame,
    PCONTEXT ContextRecord,
    void* DispatcherContext
)
{
    (void)EstablisherFrame;
    (void)ContextRecord;
    (void)DispatcherContext;

    size_t* VisitCount =
        (size_t*)ExceptionRecord->ExceptionInformation[0];
    (*VisitCount)++;
    return ExceptionContinueExecution;
}

static EXCEPTION_DISPOSITION
MtpNativeRaiseHandler(
    PEXCEPTION_RECORD ExceptionRecord,
    void* EstablisherFrame,
    PCONTEXT ContextRecord,
    void* DispatcherContext
)
{
    (void)DispatcherContext;

    MTP_NATIVE_RAISE_CONTEXT* Test =
        (MTP_NATIVE_RAISE_CONTEXT*)ExceptionRecord->ExceptionInformation[0];
    PTEB Teb = MtCurrentTeb();

    if (!Test || !Teb ||
        ExceptionRecord->ExceptionCode != (uint32_t)MTP_NATIVE_RAISE_CODE ||
        ExceptionRecord->ExceptionFlags != 0 ||
        ExceptionRecord->ExceptionRecord != NULL ||
        ExceptionRecord->ExceptionAddress != Test->ExpectedAddress ||
        ExceptionRecord->NumberParameters != 2 ||
        ExceptionRecord->ExceptionInformation[1] !=
            MTP_NATIVE_RAISE_INFORMATION ||
        EstablisherFrame != Test->ExpectedFrame ||
        (ContextRecord->ContextFlags & MT_CONTEXT_FULL) != MT_CONTEXT_FULL ||
        !ContextRecord->Rip ||
        !MT_IS_CANONICAL_USER_ADDRESS(ContextRecord->Rip) ||
        ContextRecord->Rsp < (uintptr_t)Teb->MtTib.StackLimit ||
        ContextRecord->Rsp > (uintptr_t)Teb->MtTib.StackBase) {
        if (Test) {
            Test->Failed = true;
        }
        return ExceptionContinueExecution;
    }

    Test->VisitCount++;

    // A successful MtContinue must restore this value to the syscall stub.
    ContextRecord->Rax = (uint32_t)MTP_NATIVE_RESUME_STATUS;
    return ExceptionContinueExecution;
}

static MTSTATUS
MtpTestNativeRaiseAndContinue(
    void
)
{
    EXCEPTION_REGISTRATION_RECORD Frame = { 0 };
    MTP_NATIVE_RAISE_CONTEXT Test = {
        .ExpectedFrame = &Frame,
        .ExpectedAddress = (void*)(uintptr_t)0x12345000ULL
    };
    EXCEPTION_RECORD Record = {
        .ExceptionCode = (uint32_t)MTP_NATIVE_RAISE_CODE,
        .ExceptionAddress = Test.ExpectedAddress,
        .NumberParameters = 2,
        .ExceptionInformation = {
            (uintptr_t)&Test,
            MTP_NATIVE_RAISE_INFORMATION
        }
    };

    MtpPushExceptionFrame(&Frame, MtpNativeRaiseHandler);
    MTSTATUS Status = MtRaiseException(&Record);
    MtpPopExceptionFrame(&Frame);

    if (Status != MTP_NATIVE_RESUME_STATUS) {
        return MtpExceptionTestFailure(90);
    }
    if (Test.Failed || Test.VisitCount != 1) {
        return MtpExceptionTestFailure(91);
    }
    if (MtCurrentTeb()->MtTib.ExceptionList != MT_EXCEPTION_CHAIN_END) {
        return MtpExceptionTestFailure(92);
    }
    return MT_SUCCESS;
}

static uint32_t
MtpLanguageFrameMismatch(
    PMT_LANGUAGE_FRAME Frame,
    MTSTATUS ExceptionCode,
    uintptr_t Information,
    PEXCEPTION_REGISTRATION_RECORD ExpectedNext
)
{
    PTEB Teb = MtCurrentTeb();

    if (!Teb) return 1;
    if (Frame->Linked != 1) return 2;
    if (Frame->ExceptionPointers.ExceptionRecord != &Frame->ExceptionRecord ||
        Frame->ExceptionPointers.ContextRecord != &Frame->ContextRecord) return 3;
    if (Frame->ExceptionRecord.ExceptionCode != (uint32_t)ExceptionCode) return 4;
    if (Frame->ExceptionRecord.ExceptionFlags != 0) return 5;
    if (Frame->ExceptionRecord.ExceptionRecord != NULL) return 6;
    if (Frame->ExceptionRecord.NumberParameters != 1 ||
        Frame->ExceptionRecord.ExceptionInformation[0] != Information) return 7;
    if (Frame->SearchNext != ExpectedNext) return 8;
    if ((Frame->ContextRecord.ContextFlags & MT_CONTEXT_FULL) != MT_CONTEXT_FULL) return 9;
    if (!Frame->ContextRecord.Rip ||
        !MT_IS_CANONICAL_USER_ADDRESS(Frame->ContextRecord.Rip)) return 10;
    if (Frame->ContextRecord.Rsp < (uintptr_t)Teb->MtTib.StackLimit ||
        Frame->ContextRecord.Rsp > (uintptr_t)Teb->MtTib.StackBase) return 11;
    return 0;
}

static MTSTATUS
MtpTestLanguageNormalExit(
    void
)
{
    MT_LANGUAGE_FRAME Frame = { 0 };
    PTEB Teb = MtCurrentTeb();
    int Action = MtpSaveLanguageContext(&Frame.Continuation);

    if (!Teb || Action != MtLanguageProtected) {
        return MtpExceptionTestFailure(110);
    }

    MtpEnterLanguageFrame(&Frame);
    if (Frame.Linked != 1 ||
        Teb->MtTib.ExceptionList != &Frame.Registration) {
        return MtpExceptionTestFailure(111);
    }

    MtpLeaveLanguageFrame(&Frame);
    if (Frame.Linked != 0 ||
        Teb->MtTib.ExceptionList != MT_EXCEPTION_CHAIN_END) {
        return MtpExceptionTestFailure(112);
    }

    return MT_SUCCESS;
}

static int
MtpTranslatedContinueFilter(
    PEXCEPTION_POINTERS Information,
    volatile uint32_t* FilterCount,
    volatile uint32_t* Failure
)
{
    (*FilterCount)++;
    if (!Information || !Information->ExceptionRecord ||
        !Information->ContextRecord ||
        Information->ExceptionRecord->ExceptionCode !=
            (uint32_t)MTP_LANGUAGE_CONTINUE_CODE) {
        *Failure = 1;
        return MT_EXCEPTION_EXECUTE_HANDLER;
    }

    Information->ContextRecord->Rax = MTP_LANGUAGE_RESUME_STATUS;
    return MT_EXCEPTION_CONTINUE_EXECUTION;
}

static MTSTATUS
MtpTestTranslatedLanguageSyntax(
    void
)
{
    volatile uint32_t NormalCount = 0;
    volatile uint32_t ContinueFilterCount = 0;
    volatile uint32_t ContinueFailure = 0;
    volatile uint32_t ExecuteFilterCount = 0;
    volatile uint32_t ExecuteHandlerCode = 0;

    __try {
        NormalCount++;
    }
    __except (MT_EXCEPTION_EXECUTE_HANDLER) {
        return MtpExceptionTestFailure(201);
    }

    if (NormalCount != 1 ||
        MtCurrentTeb()->MtTib.ExceptionList != MT_EXCEPTION_CHAIN_END) {
        return MtpExceptionTestFailure(202);
    }

    __try {
        EXCEPTION_RECORD Record = {
            .ExceptionCode = (uint32_t)MTP_LANGUAGE_CONTINUE_CODE
        };
        MTSTATUS Status = MtRaiseException(&Record);
        if (Status != MTP_LANGUAGE_RESUME_STATUS) {
            return MtpExceptionTestFailure(203);
        }
    }
    __except (
        MtpTranslatedContinueFilter(
            GetExceptionInformation(),
            &ContinueFilterCount,
            &ContinueFailure
        )
    ) {
        return MtpExceptionTestFailure(204 + ContinueFailure);
    }

    if (ContinueFilterCount != 1 || ContinueFailure != 0 ||
        MtCurrentTeb()->MtTib.ExceptionList != MT_EXCEPTION_CHAIN_END) {
        return MtpExceptionTestFailure(206);
    }

    __try {
        EXCEPTION_RECORD Record = {
            .ExceptionCode = (uint32_t)MTP_LANGUAGE_EXECUTE_CODE
        };
        (void)MtRaiseException(&Record);
        return MtpExceptionTestFailure(207);
    }
    __except (
        (++ExecuteFilterCount, MT_EXCEPTION_EXECUTE_HANDLER)
    ) {
        ExecuteHandlerCode = GetExceptionCode();
    }

    if (ExecuteFilterCount != 1 ||
        ExecuteHandlerCode != (uint32_t)MTP_LANGUAGE_EXECUTE_CODE ||
        MtCurrentTeb()->MtTib.ExceptionList != MT_EXCEPTION_CHAIN_END) {
        return MtpExceptionTestFailure(208);
    }

    return MT_SUCCESS;
}

static MTSTATUS
MtpTestLanguageContinueExecution(
    void
)
{
    MT_LANGUAGE_FRAME Frame = { 0 };
    volatile uint32_t FilterCount = 0;
    volatile uint32_t Failure = 0;
    int Action = MtpSaveLanguageContext(&Frame.Continuation);

    if (Action == MtLanguageProtected) {
        EXCEPTION_RECORD Record = {
            .ExceptionCode = (uint32_t)MTP_LANGUAGE_CONTINUE_CODE,
            .NumberParameters = 1,
            .ExceptionInformation = { (uintptr_t)&FilterCount }
        };

        MtpEnterLanguageFrame(&Frame);
        MTSTATUS Status = MtRaiseException(&Record);
        MtpLeaveLanguageFrame(&Frame);

        if (Status != MTP_LANGUAGE_RESUME_STATUS ||
            FilterCount != 1 || Failure != 0 ||
            MtCurrentTeb()->MtTib.ExceptionList != MT_EXCEPTION_CHAIN_END) {
            return MtpExceptionTestFailure(120);
        }
        return MT_SUCCESS;
    }

    if (Action == MtLanguageEvaluateFilter) {
        FilterCount++;
        Failure = MtpLanguageFrameMismatch(
            &Frame,
            MTP_LANGUAGE_CONTINUE_CODE,
            (uintptr_t)&FilterCount,
            MT_EXCEPTION_CHAIN_END
        );
        if (Failure != 0) {
            MtpApplyLanguageFilter(&Frame, MT_EXCEPTION_EXECUTE_HANDLER);
        }

        Frame.ContextRecord.Rax = (uint32_t)MTP_LANGUAGE_RESUME_STATUS;
        MtpApplyLanguageFilter(&Frame, MT_EXCEPTION_CONTINUE_EXECUTION);
    }

    if (Action == MtLanguageExecuteHandler && Failure != 0) {
        return MtpExceptionTestFailure(150 + Failure);
    }

    return MtpExceptionTestFailure(121 + (uint32_t)Action);
}

static MTSTATUS
MtpTestLanguageExecuteHandler(
    void
)
{
    MT_LANGUAGE_FRAME Frame = { 0 };
    volatile uint32_t FilterCount = 0;
    volatile uint32_t Failure = 0;
    int Action = MtpSaveLanguageContext(&Frame.Continuation);

    if (Action == MtLanguageProtected) {
        EXCEPTION_RECORD Record = {
            .ExceptionCode = (uint32_t)MTP_LANGUAGE_EXECUTE_CODE,
            .NumberParameters = 1,
            .ExceptionInformation = { (uintptr_t)&FilterCount }
        };

        MtpEnterLanguageFrame(&Frame);
        (void)MtRaiseException(&Record);
        return MtpExceptionTestFailure(130);
    }

    if (Action == MtLanguageEvaluateFilter) {
        FilterCount++;
        Failure = MtpLanguageFrameMismatch(
            &Frame,
            MTP_LANGUAGE_EXECUTE_CODE,
            (uintptr_t)&FilterCount,
            MT_EXCEPTION_CHAIN_END
        );
        MtpApplyLanguageFilter(&Frame, MT_EXCEPTION_EXECUTE_HANDLER);
    }

    if (Action == MtLanguageExecuteHandler) {
        if (Failure != 0) {
            return MtpExceptionTestFailure(170 + Failure);
        }
        if (FilterCount != 1 || Frame.Linked != 0 ||
            MtCurrentTeb()->MtTib.ExceptionList != MT_EXCEPTION_CHAIN_END) {
            return MtpExceptionTestFailure(131);
        }
        return MT_SUCCESS;
    }

    return MtpExceptionTestFailure(132);
}

static MTSTATUS
MtpLanguageInnerContinueSearch(
    MTP_LANGUAGE_SEARCH_CONTEXT* Test
)
{
    MT_LANGUAGE_FRAME Frame = { 0 };
    int Action = MtpSaveLanguageContext(&Frame.Continuation);

    if (Action == MtLanguageProtected) {
        EXCEPTION_RECORD Record = {
            .ExceptionCode = (uint32_t)MTP_LANGUAGE_SEARCH_CODE,
            .NumberParameters = 1,
            .ExceptionInformation = { (uintptr_t)Test }
        };

        MtpEnterLanguageFrame(&Frame);
        (void)MtRaiseException(&Record);
        return MtpExceptionTestFailure(140);
    }

    if (Action == MtLanguageEvaluateFilter) {
        Test->InnerFilterCount++;
        Test->Failure = MtpLanguageFrameMismatch(
            &Frame,
            MTP_LANGUAGE_SEARCH_CODE,
            (uintptr_t)Test,
            &Test->OuterFrame->Registration
        );
        MtpApplyLanguageFilter(&Frame, MT_EXCEPTION_CONTINUE_SEARCH);
    }

    return MtpExceptionTestFailure(141);
}

static MTSTATUS
MtpTestLanguageContinueSearch(
    void
)
{
    MT_LANGUAGE_FRAME OuterFrame = { 0 };
    MTP_LANGUAGE_SEARCH_CONTEXT Test = {
        .OuterFrame = &OuterFrame
    };
    int Action = MtpSaveLanguageContext(&OuterFrame.Continuation);

    if (Action == MtLanguageProtected) {
        MtpEnterLanguageFrame(&OuterFrame);
        MTSTATUS Status = MtpLanguageInnerContinueSearch(&Test);
        Test.Failure = 12;
        if (OuterFrame.Linked == 1) {
            MtpLeaveLanguageFrame(&OuterFrame);
        }
        return MT_FAILURE(Status) ? Status : MtpExceptionTestFailure(142);
    }

    if (Action == MtLanguageEvaluateFilter) {
        Test.OuterFilterCount++;
        uint32_t Failure = MtpLanguageFrameMismatch(
            &OuterFrame,
            MTP_LANGUAGE_SEARCH_CODE,
            (uintptr_t)&Test,
            MT_EXCEPTION_CHAIN_END
        );
        if (Failure != 0 && Test.Failure == 0) {
            Test.Failure = 20 + Failure;
        }
        MtpApplyLanguageFilter(
            &OuterFrame,
            MT_EXCEPTION_EXECUTE_HANDLER
        );
    }

    if (Action == MtLanguageExecuteHandler) {
        if (Test.Failure != 0) {
            return MtpExceptionTestFailure(180 + Test.Failure);
        }
        if (Test.InnerFilterCount != 1 ||
            Test.OuterFilterCount != 1 || OuterFrame.Linked != 0 ||
            MtCurrentTeb()->MtTib.ExceptionList != MT_EXCEPTION_CHAIN_END) {
            return MtpExceptionTestFailure(143);
        }
        return MT_SUCCESS;
    }

    return MtpExceptionTestFailure(144);
}

static MTSTATUS
MtpTestEmptyChain(
    void
)
{
    PTEB Teb = MtCurrentTeb();
    EXCEPTION_RECORD Record = { 0 };
    CONTEXT Context = { 0 };

    if (!Teb || Teb->MtTib.ExceptionList != MT_EXCEPTION_CHAIN_END) {
        return MtpExceptionTestFailure(1);
    }
    if (MtpDispatchException(&Record, &Context)) {
        return MtpExceptionTestFailure(2);
    }
    if (Teb->MtTib.ExceptionList != MT_EXCEPTION_CHAIN_END) {
        return MtpExceptionTestFailure(3);
    }
    return MT_SUCCESS;
}

static MTSTATUS
MtpTestOrderedChain(
    size_t FrameCount,
    uint32_t FailureBase
)
{
    EXCEPTION_REGISTRATION_RECORD
        Frames[MT_EXCEPTION_MAXIMUM_TRAVERSAL_COUNT] = { 0 };
    MTP_EXCEPTION_VISIT_CONTEXT Test = {
        .Frames = Frames,
        .FrameCount = FrameCount,
        .MutationIndex = FrameCount / 2,
        .MutationValue = 0x1122334455667788ULL
    };
    EXCEPTION_RECORD Record = {
        .ExceptionCode = (uint32_t)MT_ACCESS_VIOLATION,
        .NumberParameters = 1,
        .ExceptionInformation = { (uintptr_t)&Test }
    };
    CONTEXT Context = { .ContextFlags = MT_CONTEXT_FULL };

    for (size_t Index = FrameCount; Index != 0; Index--) {
        MtpPushExceptionFrame(&Frames[Index - 1], MtpVisitHandler);
    }

    bool Handled = MtpDispatchException(&Record, &Context);
    for (size_t Index = 0; Index < FrameCount; Index++) {
        MtpPopExceptionFrame(&Frames[Index]);
    }

    if (!Handled || Test.Failed || Test.VisitCount != FrameCount) {
        return MtpExceptionTestFailure(FailureBase);
    }
    if (Context.Rax != Test.MutationValue) {
        return MtpExceptionTestFailure(FailureBase + 1);
    }
    if (MtCurrentTeb()->MtTib.ExceptionList != MT_EXCEPTION_CHAIN_END) {
        return MtpExceptionTestFailure(FailureBase + 2);
    }
    return MT_SUCCESS;
}

static MTSTATUS
MtpTestSavedNext(
    void
)
{
    EXCEPTION_REGISTRATION_RECORD Frames[2] = { 0 };
    size_t VisitCount = 0;
    EXCEPTION_RECORD Record = {
        .NumberParameters = 1,
        .ExceptionInformation = { (uintptr_t)&VisitCount }
    };
    CONTEXT Context = { 0 };

    MtpPushExceptionFrame(&Frames[1], MtpSavedNextTailHandler);
    MtpPushExceptionFrame(&Frames[0], MtpMutateNextHandler);
    bool Handled = MtpDispatchException(&Record, &Context);

    // The first handler deliberately changed this link. Restore it so the
    // registration helpers can unwind the test's original chain.
    Frames[0].Next = &Frames[1];
    MtpPopExceptionFrame(&Frames[0]);
    MtpPopExceptionFrame(&Frames[1]);

    if (!Handled || VisitCount != 2) {
        return MtpExceptionTestFailure(20);
    }
    return MT_SUCCESS;
}

static uint32_t
MtpFatalCaseWorker(
    void* Parameter
)
{
    MTP_EXCEPTION_FATAL_CASE Test =
        (MTP_EXCEPTION_FATAL_CASE)(uintptr_t)Parameter;
    PTEB Teb = MtCurrentTeb();
    EXCEPTION_RECORD Record = { 0 };
    CONTEXT Context = { 0 };
    EXCEPTION_REGISTRATION_RECORD
        Frames[MT_EXCEPTION_MAXIMUM_TRAVERSAL_COUNT + 1] = { 0 };

    for (size_t Index = 0;
         Index < MT_EXCEPTION_MAXIMUM_TRAVERSAL_COUNT + 1;
         Index++) {
        Frames[Index].Next =
            Index + 1 < MT_EXCEPTION_MAXIMUM_TRAVERSAL_COUNT + 1
                ? &Frames[Index + 1]
                : MT_EXCEPTION_CHAIN_END;
        Frames[Index].Handler = MtpContinueSearchHandler;
    }

    Teb->MtTib.ExceptionList = &Frames[0];
    switch (Test) {
    case MtpFatalRecordOutsideStack:
        Teb->MtTib.ExceptionList =
            (PEXCEPTION_REGISTRATION_RECORD)(
                (uintptr_t)Teb->MtTib.StackLimit -
                _Alignof(EXCEPTION_REGISTRATION_RECORD)
            );
        break;
    case MtpFatalInvalidHandler:
        Frames[0].Handler = (PEXCEPTION_ROUTINE)(uintptr_t)UINT64_MAX;
        break;
    case MtpFatalInvalidNext:
        Frames[0].Next =
            (PEXCEPTION_REGISTRATION_RECORD)Teb->MtTib.StackBase;
        break;
    case MtpFatalSelfCycle:
        Frames[0].Next = &Frames[0];
        break;
    case MtpFatalExcessiveLength:
        break;
    case MtpFatalInvalidDisposition:
        Frames[0].Handler = MtpInvalidDispositionHandler;
        break;
    case MtpFatalNestedDisposition:
        Frames[0].Handler = MtpNestedDispositionHandler;
        break;
    case MtpFatalCollidedUnwind:
        Frames[0].Handler = MtpCollidedUnwindHandler;
        break;
    case MtpFatalNoncontinuableContinuation:
        Record.ExceptionFlags = MT_EXCEPTION_NONCONTINUABLE;
        Frames[0].Handler = MtpContinueExecutionHandler;
        break;
    default:
        return (uint32_t)MtpExceptionTestFailure(30);
    }

    (void)MtpDispatchException(&Record, &Context);
    return (uint32_t)MtpExceptionTestFailure(31 + (uint32_t)Test);
}

static MTSTATUS
MtpTestFatalCases(
    void
)
{
    for (MTP_EXCEPTION_FATAL_CASE Test = MtpFatalRecordOutsideStack;
         Test <= MtpFatalNoncontinuableContinuation;
         Test++) {
        HANDLE Thread = CreateThread(
            MtpFatalCaseWorker,
            (void*)(uintptr_t)Test
        );
        if (Thread == MT_INVALID_HANDLE) {
            return MtpExceptionTestFailure(50 + (uint32_t)Test);
        }

        uint32_t WaitResult = WaitForSingleObject(
            Thread,
            MTP_EXCEPTION_TEST_TIMEOUT_MS
        );
        if (WaitResult != WAIT_OBJECT_0) {
            TerminateThread(Thread, MT_INVALID_CHECK);
            CloseHandle(Thread);
            return MtpExceptionTestFailure(60 + (uint32_t)Test);
        }

        uint32_t ExitCode = 0;
        bool Queried = GetExitCodeThread(Thread, &ExitCode);
        bool Closed = CloseHandle(Thread);
        if (!Queried || !Closed || ExitCode != (uint32_t)MT_INVALID_PARAM) {
            return MtpExceptionTestFailure(70 + (uint32_t)Test);
        }
    }

    return MT_SUCCESS;
}

MTDLL_API MTSTATUS
MtpRunExceptionChainTests(
    void
)
{
    uint32_t AssemblyFailure = MtpRunLanguageContextAssemblyTest();
    if (AssemblyFailure != 0) {
        return MtpExceptionTestFailure(100 + AssemblyFailure);
    }

    MTSTATUS Status = MtpTestEmptyChain();
    if (MT_FAILURE(Status)) return Status;

    Status = MtpTestOrderedChain(2, 10);
    if (MT_FAILURE(Status)) return Status;

    Status = MtpTestOrderedChain(
        MT_EXCEPTION_MAXIMUM_TRAVERSAL_COUNT,
        14
    );
    if (MT_FAILURE(Status)) return Status;

    Status = MtpTestSavedNext();
    if (MT_FAILURE(Status)) return Status;

    Status = MtpTestNativeRaiseAndContinue();
    if (MT_FAILURE(Status)) return Status;

    Status = MtpTestLanguageNormalExit();
    if (MT_FAILURE(Status)) return Status;

    Status = MtpTestTranslatedLanguageSyntax();
    if (MT_FAILURE(Status)) return Status;

    Status = MtpTestLanguageExecuteHandler();
    if (MT_FAILURE(Status)) return Status;

    Status = MtpTestLanguageContinueSearch();
    if (MT_FAILURE(Status)) return Status;

    Status = MtpTestRealProcessorExceptions();
    if (MT_FAILURE(Status)) return Status;

    Status = MtpTestUnhandledProcessorExceptions();
    if (MT_FAILURE(Status)) return Status;

    Status = MtpTestHandlerFault();
    if (MT_FAILURE(Status)) return Status;

    Status = MtpTestFatalCases();
    if (MT_FAILURE(Status)) return Status;

    Status = MtpTestRealAccessViolations();
    if (MT_FAILURE(Status)) return Status;

    Status = MtpTestUnhandledAccessViolation();
    if (MT_FAILURE(Status)) return Status;

    return MtpTestLanguageContinueExecution();
}
