/*++

Module Name:

    exception.c

Purpose:

    This translation unit contains the implementation of user mode exception handling in MatanelOS, user mode side.

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "../../../shared/include/mtexception.h"
#include "../../../shared/include/MatanelOS.h"
#include "includes/mtdll.h"
#include "includes/annotations.h"

// forward
static
bool
MtpDispatchExceptionFrom(
    PEXCEPTION_REGISTRATION_RECORD Record,
    PEXCEPTION_RECORD ExceptionRecord,
    PCONTEXT ContextRecord
);

NORETURN
FORCEINLINE
void
MtpExceptionFailureTermination(
    void
)

{
    // The function can also terminate the thread with logging
    // When we have an event viewer in the next 2 decades, we can report here too
    TerminateThread(MtCurrentThread(), MT_INVALID_PARAM);
    UNREACHABLE_CODE();
}

void
MtpPushExceptionFrame(
    PEXCEPTION_REGISTRATION_RECORD Frame,
    PEXCEPTION_ROUTINE Handler
)

{
    // Dont allow NULL, obviously
    if (!Frame || !Handler) {
        MtpExceptionFailureTermination();
    }

    PTEB Teb = MtCurrentTeb();
    if (!Teb) {
        MtpExceptionFailureTermination();
    }

    // Set the next exception frame as the current one
    // And set the current one as the parameter
    // Along with its supplied handler
    // Basically a singly linked list
    if (Teb->MtTib.ExceptionList) {
        Frame->Next = Teb->MtTib.ExceptionList;
        Frame->Handler = Handler;

        // Publish the initialized frame before making it reachable from the TEB.
        MmFullBarrier();
        Teb->MtTib.ExceptionList = Frame;
    }
    else {
        MtpExceptionFailureTermination();
    }
}

void
MtpPopExceptionFrame(
    PEXCEPTION_REGISTRATION_RECORD Frame
)

{
    if (!Frame) {
        MtpExceptionFailureTermination();
    }

    // A popped frame must be the current head
    PTEB Teb = MtCurrentTeb();
    if (!Teb) {
        MtpExceptionFailureTermination();
    }

    if (Teb->MtTib.ExceptionList != Frame) {
        MtpExceptionFailureTermination();
    }

    // Move on to the next frame
    Teb->MtTib.ExceptionList = Frame->Next;
    MmFullBarrier();
}

bool
MtpDispatchException(
    PEXCEPTION_RECORD ExceptionRecord,
    PCONTEXT ContextRecord
)

{
    PTEB Teb = MtCurrentTeb();

    if (!ExceptionRecord || !ContextRecord || !Teb) {
        MtpExceptionFailureTermination();
    }

    return MtpDispatchExceptionFrom(
        Teb->MtTib.ExceptionList,
        ExceptionRecord,
        ContextRecord
    );
}

// called from Assembly (apcdispatch.asm)
NORETURN
void
MtpUserExceptionDispatcher(
    PEXCEPTION_RECORD ExceptionRecord,
    PCONTEXT ContextRecord
)
{
    // Preserve the original status before handlers can modify the record.
    MTSTATUS ExceptionCode = (MTSTATUS)ExceptionRecord->ExceptionCode;
    bool HadRegistration =
        MtCurrentTeb()->MtTib.ExceptionList != MT_EXCEPTION_CHAIN_END;

    if (MtpDispatchException(ExceptionRecord, ContextRecord)) {
        // A handler selected ContinueExecution.
        MtContinue(ContextRecord);
    }

    // The complete chain selected ContinueSearch.
    MtTerminateThread(MtCurrentThread(),
        HadRegistration ? MT_GENERAL_FAILURE : ExceptionCode);
    UNREACHABLE_CODE();
}

static
bool
MtpDispatchExceptionFrom(
    PEXCEPTION_REGISTRATION_RECORD Record,
    PEXCEPTION_RECORD ExceptionRecord,
    PCONTEXT ContextRecord
)

{
    if (!ExceptionRecord || !ContextRecord) {
        MtpExceptionFailureTermination();
    }

    PTEB Teb = MtCurrentTeb();
    if (!Teb) {
        MtpExceptionFailureTermination();
    }

    // Walk the exception handler list
    int Iteration = 0;

    while (Record != MT_EXCEPTION_CHAIN_END) {
        Iteration++;

        if (Iteration > MT_EXCEPTION_MAXIMUM_TRAVERSAL_COUNT) {
            // Traversing over the maximum count is a malformed loop, no exception should be handled like this
            MtpExceptionFailureTermination();
        }

        // Validate some stuff before calling the handler.
        // First, validate that the record indeed exists in the user stack.
        uintptr_t StackLimit = (uintptr_t)Teb->MtTib.StackLimit;
        uintptr_t StackBase = (uintptr_t)Teb->MtTib.StackBase;

        // Validate the stack base isnt over (or to be over) the stack limit.
        if (StackBase <= StackLimit ||
            StackBase - StackLimit < sizeof(*Record)) {
            MtpExceptionFailureTermination();
        }

        uintptr_t Address = (uintptr_t)Record;

        // Validate the record is inside the stack, and that the stack is aligned.
        if (Address < StackLimit ||
            Address > StackBase - sizeof(*Record) ||
            Address % _Alignof(EXCEPTION_REGISTRATION_RECORD) != 0) {
            MtpExceptionFailureTermination();
        }

        // Validate the handler is non-NULL and is a canonical user address.
        if (!Record->Handler ||
            !MT_IS_CANONICAL_USER_ADDRESS(Record->Handler)) {
            MtpExceptionFailureTermination();
        }

        // Save the next pointer.
        PEXCEPTION_REGISTRATION_RECORD Next = Record->Next;

        // Lets let it handle.
        // Invoke the low level registration handler and inspect its disposition.
        EXCEPTION_DISPOSITION Disposition = Record->Handler(
            ExceptionRecord,
            Record,
            ContextRecord,
            Next
        );

        // Act based on handler return value
        if (Disposition == ExceptionContinueSearch) {

            // Validate the next one isnt somehow a back link
            if (Next != MT_EXCEPTION_CHAIN_END &&
                (uintptr_t)Next <= Address) {
                MtpExceptionFailureTermination();
            }

            // Why not use Record->Next?
            // Because the Handler may change itself at any time, that includes its next pointer
            // And we cant let corruption or anything basically break the list traversal
            // So we save the next ptr.
            Record = Next;
            continue;
        }

        else if (Disposition == ExceptionContinueExecution) {
            // We must not continue execution if the flag does not permit us.
            if (ExceptionRecord->ExceptionFlags & MT_EXCEPTION_NONCONTINUABLE) {
                MtpExceptionFailureTermination();
            }

            return true;
        }

        else {
            // Either nested, collided-unwind or anything else mysterious
            // Just fail
            MtpExceptionFailureTermination();
        }
    }

    return false;
}

static
EXCEPTION_DISPOSITION
MtpLanguageExceptionHandler(
    PEXCEPTION_RECORD ExceptionRecord,
    void* EstablisherFrame,
    PCONTEXT ContextRecord,
    void* DispatcherContext
)

{
    if (!ExceptionRecord || !EstablisherFrame || !ContextRecord) {
        MtpExceptionFailureTermination();
    }

    // Recover language frame
    PMT_LANGUAGE_FRAME Frame = CONTAINING_RECORD(EstablisherFrame, MT_LANGUAGE_FRAME, Registration);

    if (Frame->Linked != 1) {
        MtpExceptionFailureTermination();
    }

    Frame->ExceptionRecord = *ExceptionRecord;
    Frame->ContextRecord = *ContextRecord;

    Frame->ExceptionPointers.ExceptionRecord = &Frame->ExceptionRecord;
    Frame->ExceptionPointers.ContextRecord = &Frame->ContextRecord;

    Frame->SearchNext =
        (PEXCEPTION_REGISTRATION_RECORD)DispatcherContext;

    // Restore the context
    MtpRestoreLanguageContextForFilter(
        &Frame->Continuation,
        MtLanguageEvaluateFilter
    );

    UNREACHABLE_CODE();
}

void 
MtpEnterLanguageFrame(
    PMT_LANGUAGE_FRAME Frame
)

{
    // Reject nullptr or frames that are already in the exception chain.
    if (!Frame || Frame->Linked != 0) {
        MtpExceptionFailureTermination();
    }

    Frame->ExceptionPointers.ExceptionRecord = &Frame->ExceptionRecord;
    Frame->ExceptionPointers.ContextRecord = &Frame->ContextRecord;
    Frame->SearchNext = MT_EXCEPTION_CHAIN_END;

    // Set before publishing the registration
    Frame->Linked = 1;

    MtpPushExceptionFrame(&Frame->Registration, MtpLanguageExceptionHandler);
}

void 
MtpLeaveLanguageFrame(
    PMT_LANGUAGE_FRAME Frame
)

{
    if (!Frame || Frame->Linked != 1) {
        MtpExceptionFailureTermination();
    }

    MtpPopExceptionFrame(&Frame->Registration);
    Frame->Linked = 0;
}

NORETURN
void
MtpApplyLanguageFilter(
    PMT_LANGUAGE_FRAME Frame,
    int FilterResult
)

{
    // Reject nullptr frames or unlinked ones
    if (!Frame || Frame->Linked != 1) MtpExceptionFailureTermination();

    // Execute based on the result gotten from the filter __except (FILTER)
    if (FilterResult == MT_EXCEPTION_CONTINUE_EXECUTION) {
        if (Frame->ExceptionRecord.ExceptionFlags & MT_EXCEPTION_NONCONTINUABLE) {
            MtpExceptionFailureTermination();
        }

        // Continue the execution, call MtContinue
        MtContinue(&Frame->ContextRecord);
    }
    else if (FilterResult == MT_EXCEPTION_CONTINUE_SEARCH) {

        // Move on to the next handler if any.
        bool Handled = MtpDispatchExceptionFrom(Frame->SearchNext, &Frame->ExceptionRecord, &Frame->ContextRecord);

        if (!Handled) {
            // Exception isnt handled, terminate the thread.
            TerminateThread(MtCurrentThread(), Frame->ExceptionRecord.ExceptionCode);
            UNREACHABLE_CODE();
        }

        // Exception is handled, we can continue.
        MtContinue(&Frame->ContextRecord);
    }
    else if (FilterResult == MT_EXCEPTION_EXECUTE_HANDLER) {
        // We execute the except handler here
        // Walk through the handler list.
        PTEB Teb = MtCurrentTeb();
        if (!Teb) MtpExceptionFailureTermination();

        PEXCEPTION_REGISTRATION_RECORD Record = Teb->MtTib.ExceptionList;

        int Iteration = 0;
        while (Record != MT_EXCEPTION_CHAIN_END) {
            if (++Iteration > MT_EXCEPTION_MAXIMUM_TRAVERSAL_COUNT) {
                MtpExceptionFailureTermination();
            }

            // Validate some stuff before calling the handler.
            // First, validate that the record indeed exists in the user stack.
            uintptr_t StackLimit = (uintptr_t)Teb->MtTib.StackLimit;
            uintptr_t StackBase = (uintptr_t)Teb->MtTib.StackBase;

            // Validate the stack base isnt over (or to be over) the stack limit.
            if (StackBase <= StackLimit ||
                StackBase - StackLimit < sizeof(*Record)) {
                MtpExceptionFailureTermination();
            }

            uintptr_t Address = (uintptr_t)Record;

            // Validate the record is inside the stack, and that the stack is aligned.
            if (Address < StackLimit ||
                Address > StackBase - sizeof(*Record) ||
                Address % _Alignof(EXCEPTION_REGISTRATION_RECORD) != 0) {
                MtpExceptionFailureTermination();
            }

            // Validate the handler is non-NULL and is a canonical user address.
            if (!Record->Handler ||
                !MT_IS_CANONICAL_USER_ADDRESS(Record->Handler)) {
                MtpExceptionFailureTermination();
            }

            // Next ptr must not be a backlink.
            if ((uintptr_t)Record->Next <= (uintptr_t)Record) {
                MtpExceptionFailureTermination();
            }

            if (&Frame->Registration == Record) {
                // We have found the __except handler, now redirect execution to it
                // Use MtContinue and not restoring via the language handler, since now we exit the exception
                // and the kernel must untick the UserExceptionActive.

                // Before unlinking, verify the saved successor is indeed our next exception registration ptr.
                if (Frame->SearchNext != Record->Next) {
                    MtpExceptionFailureTermination();
                }

                // Validate the next is inside the stack and has all of the rules as an exception registration record should have (alignment canonical etc)
                if (Frame->SearchNext != MT_EXCEPTION_CHAIN_END) {
                    uintptr_t NextAddress = (uintptr_t)Frame->SearchNext;

                    if (NextAddress < StackLimit ||
                        NextAddress > StackBase - sizeof(*Record) ||
                        NextAddress % _Alignof(EXCEPTION_REGISTRATION_RECORD) != 0 ||
                        !Frame->SearchNext->Handler ||
                        !MT_IS_CANONICAL_USER_ADDRESS(Frame->SearchNext->Handler)) {
                        MtpExceptionFailureTermination();
                    }
                }

                Teb->MtTib.ExceptionList = Frame->SearchNext;
                MmFullBarrier();
                Frame->Linked = 0;

                CONTEXT HandlerContext = Frame->ContextRecord;

                HandlerContext.Rbx = Frame->Continuation.Rbx;
                HandlerContext.Rbp = Frame->Continuation.Rbp;
                HandlerContext.R12 = Frame->Continuation.R12;
                HandlerContext.R13 = Frame->Continuation.R13;
                HandlerContext.R14 = Frame->Continuation.R14;
                HandlerContext.R15 = Frame->Continuation.R15;
                HandlerContext.Rsp = Frame->Continuation.Rsp;
                HandlerContext.Rip = Frame->Continuation.Rip;
                HandlerContext.Rax = MtLanguageExecuteHandler;

                MtContinue(&HandlerContext);
                UNREACHABLE_CODE();
            }

            Record = Record->Next;
        }

        // Reaching here means finding an __except handler has failed.
        TerminateThread(MtCurrentThread(), Frame->ExceptionRecord.ExceptionCode);
        UNREACHABLE_CODE();
    }
    else {
        MtpExceptionFailureTermination();
    }
}

/*++

Routine Description:

    Raises a software exception in the calling user mode thread.

Arguments:

    [IN] ExceptionCode - The application-defined exception code.

    [IN] ExceptionFlags - The exception behavior flags.

    [IN] NumberOfArguments - The number of exception arguments supplied.

    [IN OPTIONAL] Arguments - The exception argument array.

Return Values:

    None. A continuable exception can resume at the instruction following the
    native raise request; an unhandled exception terminates the thread.

--*/

MTDLL_API
NOINLINE
void
RaiseException(
    IN uint32_t ExceptionCode,
    IN uint32_t ExceptionFlags,
    IN uint32_t NumberOfArguments,
    _In_Opt const uintptr_t* Arguments
)

{
    EXCEPTION_RECORD Record = { 0 };
    Record.ExceptionCode = ExceptionCode;
    Record.ExceptionFlags = ExceptionFlags & MT_EXCEPTION_VALID_FLAGS;
    Record.ExceptionAddress = (void*)(uintptr_t)__builtin_return_address(0);

    if (NumberOfArguments > MT_EXCEPTION_MAXIMUM_PARAMETERS) {
        NumberOfArguments = MT_EXCEPTION_MAXIMUM_PARAMETERS;
    }

    Record.NumberParameters = NumberOfArguments;

    // Copy each argument based on the number given or maximum.
    for (uint32_t i = 0; i < NumberOfArguments; i++) {
        Record.ExceptionInformation[i] = Arguments[i];
    }

    // Raise it.
    (void)MtRaiseException(&Record);
}
