/*++

Module Name:

    thrdldr.c

Purpose:

    This translation unit contains the implementation of loading threads into the current process, including its main thread.

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "../includes/mtdll.h"
#include "../includes/processthreadsapi.h"
#include "../includes/exports.h"

static
void
LdrpInitializeThreadPointer(
    IN void* ThreadPointer
)
{
    __asm__ volatile (
        "wrfsbase %0"
        :
    : "r"(ThreadPointer)
        : "memory"
        );
}

static
void
LdrpInitializeBase(
    IN PTEB TebPointer
)

/*++

    Routine description:

        Publishes the supplied TEB as the current thread's GS base.

    Arguments:

        [IN] TebPointer - The TEB to publish for the current thread.

    Return Values:

        None.

    Notes:

        The TEB must remain valid for the lifetime of the thread.

--*/

{
    __asm__ volatile (
        "wrgsbase %0"
        :
    : "r"(TebPointer)
        : "memory"
        );
}

MTDLL_API
void
LdrInitializeThread(
    IN PTEB Teb,
    IN PPEB Peb,
    IN uint64_t EntryPoint,
    IN uintptr_t ThreadParameter
)

/*++

    Routine description:

        Initializes a thread's TEB, invokes its start routine, and requests
        thread termination after the start routine returns.

    Arguments:

        [IN] Teb - The TEB assigned to the new thread.
        [IN] Peb - The process environment block owning the thread.
        [IN] EntryPoint - The thread start routine address.
        [IN] ThreadParameter - The value passed to the start routine.

    Return Values:

        None. The routine normally terminates the current thread before it can
        return.

--*/

{
    // Link the TEB to the PEB.
    Teb->ProcessEnvironmentBlock = Peb;

    // Set GS
    LdrpInitializeBase(Teb);

    MTSTATUS Status = LdrpInitializeThreadTls(
        Teb,
        Peb
    );

    if (MT_FAILURE(Status)) {
        TerminateThread(
            MtCurrentThread(),
            Status
        );

        // TerminateThread on ourselves should not return.
        TerminateProcess(
            MtCurrentProcess(),
            Status
        );
    }

    // Set FS
    LdrpInitializeThreadPointer(Teb->ThreadPointer);

    // Jump to entry point.
    uint32_t RetVal = ((uint32_t (*)(uintptr_t))EntryPoint)(ThreadParameter);

    Status = LdrpDestroyThreadTls(Teb);
    if (MT_FAILURE(Status)) {
        TerminateProcess(
            MtCurrentProcess(),
            Status
        );
    }

    // Returned from a thread.
    // So we call to terminate the thread.
    TerminateThread(MtCurrentThread(), RetVal);

    // This should effictively be a no-return, but if we did return from TerminateThread, we are somehow the last thread of the process, and ExitProcess wasnt called.
    TerminateProcess(MtCurrentProcess(), MT_GENERAL_FAILURE);
}
