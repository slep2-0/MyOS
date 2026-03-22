/*++

Module Name:

    thrdldr.c

Purpose:

    This translation unit contains the implementation of loading threads into the current process, including its main thread.s

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "../includes/mtdll.h"
#include "../includes/processthreadsapi.h"
#include "../includes/exports.h"

static
void
LdrpInitializeBase(
    IN PTEB TebPointer
)

{
    __asm__ volatile (
        "wrgsbase %0"
        :
    : "r"(TebPointer)
        : "memory"
        );
}

void
LdrInitializeThread(
    IN PTEB Teb,
    IN PPEB Peb,
    IN uint64_t EntryPoint,
    IN uintptr_t ThreadParameter
)

{
    // Link the TEB to the PEB.
    Teb->ProcessEnvironmentBlock = Peb;

    // Set GS
    LdrpInitializeBase(Teb);

    // Jump to entry point.
    uint32_t RetVal = ((uint32_t (*)(uintptr_t))EntryPoint)(ThreadParameter);

    // Returned from a thread.
    // So we call to terminate the thread.
    // Todo custom retval. (ExitStatus)
    TerminateThread(MtCurrentThread(), RetVal);

    // This should effictively be a no-return, but if we did return from TerminateThread, we are somehow the last thread of the process, and ExitProcess wasnt called.
    TerminateProcess(MtCurrentProcess(), MT_GENERAL_FAILURE);
}