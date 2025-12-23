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
    // This is unused, or atleast should be changed.
    // Since the try except macros are expected to work within interrupts (like a page fault), since RBP,RSP are saved (and General Purpose Registers)
    // So simply jumping to there WILL NOT work, we must restore all general registers (which would require support handling in the try try_end; macros, and not only linker support
    // So for now, this should stay unused.
    // This will be used (probably) for user mode SEH.

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