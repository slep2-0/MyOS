/*++

Module Name:

    raise.c

Purpose:

    This translation unit contains the implementation of raising status codes in the kernel.

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "../../includes/exception.h"
#include "../../includes/ps.h"

void
ExpRaiseStatus(
    IN MTSTATUS Status,
    IN uint64_t Rip
)

{
    PETHREAD CurrentThread = PsGetCurrentThread();
    // Set status.
    CurrentThread->LastStatus = Status;

    // Dispatch exception. (RIP should be instruction that made the call, and not the retaddr)
    uint64_t HandlerAddress = ExpFindKernelModeExceptionHandler(Rip - 1);
    if (HandlerAddress != 0) {
        // Jump to exception handler.
        __asm__ volatile (
            "jmp *%0"
            :
        : "r"(HandlerAddress)
            );
    }
}