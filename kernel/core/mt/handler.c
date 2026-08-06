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
#include "../../includes/mg.h"
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
    PITHREAD CurrentThread = MeGetCurrentThread();

    // Set previous mode to user mode, this is a system call.
    // The reason why this is set to UserMode because any pointers given to the system calls
    // must be validated (and try except only happens when previousmode is usermode)
    CurrentThread->PreviousMode = UserMode;

    // Increment system call count (cool)
    MeGetCurrentProcessor()->SystemCallCount++;

    // Just for future incase. (this must be kept here since after interrupts are enabled UserRsp could very much change)
    // DO NOT Access the RSP in PTRAP_FRAME, it does not exist.
    // And DO NOT grab the UserRsp after this sti call, as it may change immediately even, save it to a local.
    //uint64_t* UserStack = (uint64_t*)MeGetCurrentProcessor()->UserRsp;

    // Publish the syscall frame before enabling preemption. A timer may switch
    // this thread immediately after STI, and blocking syscalls need this frame.
    CurrentThread->SyscallTrap = TrapFrame;

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
        gop_printf(COLOR_WHITE, "**{SYSCALL-FAIURE} (%lu) Syscall number passed %lu when its over max syscalls or NULL.", MeGetCurrentProcessor()->SystemCallCount, SyscallNumber);
        *ReturnValue = MT_INVALID_SYSTEM_SERVICE;
        goto Exit;
    }

    // Arugments are in RDI RSI RDX R10 (not RCX in Syscalls, since CPU clobbers it for RIP) R8 R9
    // Above 6 arguments we receive from user stack.
    // For now, support 6.
    // To support more args we need a syscall that actually takes more than 6 args
    uint64_t Arg1 = TrapFrame->rdi;
    uint64_t Arg2 = TrapFrame->rsi;
    uint64_t Arg3 = TrapFrame->rdx;
    uint64_t Arg4 = TrapFrame->r10;
    uint64_t Arg5 = TrapFrame->r8;
    uint64_t Arg6 = TrapFrame->r9;

    gop_printf(COLOR_WHITE, "**IN SYSCALL (%lu), NUMBER: %lu | ARG1: %lx | ARG2: %lx | ARG3: %lx**\n", MeGetCurrentProcessor()->SystemCallCount, SyscallNumber, Arg1, Arg2, Arg3);
    
    // Todo regular SSDT. (with limits, no direct indexing)
    *ReturnValue = Ssdt[SyscallNumber](Arg1, Arg2, Arg3, Arg4, Arg5, Arg6);

Exit: {
    MeDisableInterrupts();
    // SYSCALL saves only the 15 GPRs followed by user RSP. RCX and R11 carry
    // the architectural return RIP/RFLAGS. Expand that compact layout into a
    // complete CPL3 frame so user APC injection has real CS/SS/RSP fields.
    TRAP_FRAME UserFrame;
    kmemset(&UserFrame, 0, sizeof(UserFrame));
    kmemcpy(&UserFrame, TrapFrame, FIELD_OFFSET(TRAP_FRAME, vector));
    UserFrame.rip = TrapFrame->rcx;
    UserFrame.cs = USER_CS;
    UserFrame.rflags = TrapFrame->r11;
    UserFrame.rsp = TrapFrame->vector;
    UserFrame.ss = USER_SS;
    MePrepareUserDispatchForReturn(&UserFrame);

    kmemcpy(
        TrapFrame,
        &UserFrame,
        FIELD_OFFSET(TRAP_FRAME, vector)
    );

    TrapFrame->rcx = UserFrame.rip;
    TrapFrame->r11 = UserFrame.rflags;
    TrapFrame->vector = UserFrame.rsp;

    CurrentThread->SyscallTrap = NULL;
    }
}
