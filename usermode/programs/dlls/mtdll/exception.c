/*++

Module Name:

    exception.c

Purpose:

    This translation unit contains the implementation of user mode exception handling in MatanelOS, user mode side.

Author:

    slep (Matanel) 2026.

Revision History:

--*/


/*
Try except translator looks like this:

__try {
    Work();
}
__except (MyFilter(GetExceptionInformation())) {
    Recover();
}

Turns to:


{
    MT_LANGUAGE_EXCEPTION_FRAME __mt_frame_42 = { 0 };

    MT_LANGUAGE_SCOPE_GUARD __mt_guard_42
        __attribute__((cleanup(MtpCleanupLanguageFrame))) = {
            .Frame = &__mt_frame_42
        };

    int __mt_state_42 =
        MtpEnterLanguageFrame(&__mt_frame_42);

    if (__mt_state_42 == MtLanguageEnterTry) {
        Work();

        MtpLeaveLanguageFrame(&__mt_frame_42);
        goto __mt_after_42;
    }

    int __mt_filter_42 =
        MyFilter(&__mt_frame_42.ExceptionPointers);

    MtpApplyLanguageFilter(
        &__mt_frame_42,
        __mt_filter_42,
        &&__mt_handler_42
    );

    UNREACHABLE_CODE();

__mt_handler_42:
    Recover();

__mt_after_42:
    ;
}

*/

#include <mtexception.h>
#include <MatanelOS.h>
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

/*++

    Routine description:

        Terminates the current process after user exception dispatch cannot continue.

    Arguments:

        None.

    Return Values:

        None.

--*/

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

/*++

    Routine description:

        Links an initialized exception registration record into the current
        thread's exception chain.

    Arguments:

        [IN OUT] Frame - The registration record to link.
        [IN] Handler - The routine invoked during exception dispatch.

    Return Values:

        None. Invalid input terminates the current thread.

--*/

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

/*++

    Routine description:

        Removes the current head registration record from the exception chain.

    Arguments:

        [IN OUT] Frame - The registration record to unlink.

    Return Values:

        None. A frame that is not the current head terminates the current
        thread.

--*/

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

/*++

    Routine description:

        Dispatches an exception through the current thread's language frame
        chain.

    Arguments:

        [IN] ExceptionRecord - The exception record to dispatch.
        [IN OUT] ContextRecord - The context associated with the exception.

    Return Values:

        true when a handler selects continue-execution, or false when the
        chain is exhausted.

--*/

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

/*++

    Routine description:

        Enters the user-mode exception dispatcher after the kernel publishes a
        prepared exception frame.

    Arguments:

        [IN] ExceptionRecord - The exception record to dispatch.
        [IN OUT] ContextRecord - The interrupted user context.

    Return Values:

        Does not return. It continues execution through MtContinue or
        terminates the current thread when no handler accepts the exception.

--*/
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

/*++

    Routine description:

        Walks an exception chain from a specified registration record.

    Arguments:

        [IN] Record - Exception registration record being visited.
        [IN] ExceptionRecord - Exception record being dispatched.
        [IN] ContextRecord - Register context associated with the exception.

    Return Values:

        true when a handler accepts the exception, or false when the chain is exhausted.

--*/

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

/*++

    Routine description:

        Evaluates a translated language frame during exception-chain search.

    Arguments:

        [IN] ExceptionRecord - Exception record being dispatched.
        [IN] EstablisherFrame - Registration frame whose handler is being invoked.
        [IN] ContextRecord - Register context associated with the exception.
        [IN] DispatcherContext - Dispatcher-owned traversal state for the exception chain.

    Return Values:

        The exception disposition selected by the handler.

--*/

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

MTDLL_API
void 
MtpEnterLanguageFrame(
    PMT_LANGUAGE_FRAME Frame
)

/*++

    Routine description:

        Initializes and links a language frame for the current protected scope.

    Arguments:

        [IN OUT] Frame - The language frame to initialize and link.

    Return Values:

        None. Invalid or already linked frames terminate the current thread.

--*/

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

MTDLL_API
void 
MtpLeaveLanguageFrame(
    PMT_LANGUAGE_FRAME Frame
)

/*++

    Routine description:

        Unlinks a language frame after its protected scope completes.

    Arguments:

        [IN OUT] Frame - The linked language frame to remove.

    Return Values:

        None. Invalid or unlinked frames terminate the current thread.

--*/

{
    if (!Frame || Frame->Linked != 1) {
        MtpExceptionFailureTermination();
    }

    MtpPopExceptionFrame(&Frame->Registration);
    Frame->Linked = 0;
}

MTDLL_API
NORETURN
void
MtpApplyLanguageFilter(
    PMT_LANGUAGE_FRAME Frame,
    int FilterResult
)

/*++

    Routine description:

        Applies a filter disposition and transfers control to execution,
        handler, or continued exception search.

    Arguments:

        [IN OUT] Frame - The active language frame.
        [IN] FilterResult - The selected exception disposition.

    Return Values:

        Does not return.

--*/

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

/*++

    Routine description:

        Raises a software exception in the calling user thread.

    Arguments:

        [IN] ExceptionCode - Status code assigned to the software exception.
        [IN] ExceptionFlags - Flags describing exception delivery constraints.
        [IN] NumberOfArguments - Number of valid exception information values.
        [IN] Arguments - Exception information values supplied by the caller.

    Return Values:

        None.

--*/

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
