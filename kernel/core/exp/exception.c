/*++

Module Name:

    exception.c

Purpose:

    This translation unit contains the implementation of exception checking & handling in MatanelOS (_try _except macros)

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "../../includes/exception.h"
#include "../../includes/ps.h"

bool
ExpIsExceptionHandlerPresent(
    IN PETHREAD Thread
)

/*++

    Routine description:

        Checks if an exception handler is present in the current thread context.

    Arguments:

        [IN] PETHREAD Thread - The thread to check on.

    Return Values:

        True if present, false otherwise.

--*/

{
    if (Thread) {
        if (Thread->ExceptionRegistration.Handler != NULL) {
            return true;
        }
        else {
            return false;
        }
    }
    return false;
}

void
ExpDispatchException(
    IN PTRAP_FRAME TrapFrame,
    IN PCONTEXT ContextRecord,
    IN PEXCEPTION_RECORD ExceptionRecord
)

/*++

    Routine description:
        (UNUSED)
        Changes the trap frame to point to the _except handler of the thread.

    Arguments:

        [IN] PTRAP_FRAME trap - Pointer to Trap frame of the thread.
        [IN] PCONTEXT ContextRecord - Pointer to Context record of the thread (saved in _try by thread)
        [IN] PEXCEPTION_RECORD ExceptionRecord - Pointer to Exception record of the thread

    Return Values:

        None.

--*/

{
    if (ExpIsExceptionHandlerPresent(PsGetCurrentThread())) {
        // Change trap frame to context record set by thread. (except RIP)
        TrapFrame->rsp = ContextRecord->Rsp;
        TrapFrame->rflags = ContextRecord->RFlags;

        // General-purpose registers from the context frame
        TrapFrame->r15 = ContextRecord->R15;
        TrapFrame->r14 = ContextRecord->R14;
        TrapFrame->r13 = ContextRecord->R13;
        TrapFrame->r12 = ContextRecord->R12;

        TrapFrame->r11 = ContextRecord->R11;
        TrapFrame->r10 = ContextRecord->R10;
        TrapFrame->r9 = ContextRecord->R9;
        TrapFrame->r8 = ContextRecord->R8;

        TrapFrame->rbp = ContextRecord->Rbp;
        TrapFrame->rdi = ContextRecord->Rdi;
        TrapFrame->rsi = ContextRecord->Rsi;

        TrapFrame->rcx = ContextRecord->Rcx;
        TrapFrame->rbx = ContextRecord->Rbx;
        TrapFrame->rdx = ContextRecord->Rdx;
        TrapFrame->rax = ContextRecord->Rax;

        // Enumerate all handlers, if one returned FIXME TODO (Decide between return value approach or completely different approach, i scrapped the exception handling idea for now, too complicated and messy, id rather work on memory
    }


}