/*++

Module Name:

    syscall.c

Purpose:

    This module contains the implementation of the syscall C handler.

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "../../includes/mt.h"
#include "../../includes/me.h"
#include "../../includes/ps.h"
#include "../../mtstatus.h"

extern SyscallHandler Ssdt[];

void
MtSyscallHandler(
    IN PTRAP_FRAME TrapFrame
)

/*++

    Routine description:

        Handles a system call from user mode.

    Arguments:

        The TRAP_FRAME of syscall entry.

    Return Values:

        Decided by the system call, could be void, or MTSTATUS (zero extended to RAX)

    Notes:

        This function must be called ONLY FROM the MtSyscallEntry routine in assembly.

--*/

{
    // Set previous mode to user mode, this is a system call.
    MeGetCurrentThread()->PreviousMode = UserMode;

    // Enable interrupts, its safe now.
    __sti();    
    // Grab arguments
    // The return value is stored in RAX, and I dont want to do more assembly
    // Spare me.
    uint64_t* ReturnValue = &TrapFrame->rax;

    // Syscall number is in RAX.
    uint64_t SyscallNumber = TrapFrame->rax;

    // >= because 256 is an invalid index in the array (0-255)
    if (SyscallNumber >= MAX_SYSCALLS || Ssdt[SyscallNumber] == NULL) {
        *ReturnValue = MT_INVALID_PARAM;
        goto Return;
    }

    // Arugments are in RDI RSI RDX R10 (not RCX in Syscalls, since CPU clobbers it for RIP) R8 R9
    // Above 6 arguments we receive from user stack.
    // For now, support 6 (TODO ProbeForRead)
    uint64_t Arg1 = TrapFrame->rdi;
    uint64_t Arg2 = TrapFrame->rsi;
    uint64_t Arg3 = TrapFrame->rdx;
    uint64_t Arg4 = TrapFrame->r10;
    uint64_t Arg5 = TrapFrame->r8;
    uint64_t Arg6 = TrapFrame->r9;

    // Just for future incase.
    //uint64_t* UserStack = (uint64_t*)MeGetCurrentProcessor()->UserRsp;
    
    *ReturnValue = Ssdt[SyscallNumber](Arg1, Arg2, Arg3, Arg4, Arg5, Arg6);
Return:
    return;
}