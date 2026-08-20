/*++

Module Name:

    mt.h

Purpose:

    This module contains the header files & prototypes required for user mode interactions with kernel services. (System Calls)

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#ifndef X86_MATANEL_MT_H
#define X86_MATANEL_MT_H

#include "core.h"
#include "../../shared/include/mtnative.h"

// Maximum number of syscalls
#define MAX_SYSCALLS 256
typedef uint64_t(*SyscallHandler)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

void
MtSetupSyscall(
    void
);

void
MtSyscallHandler(
    IN PTRAP_FRAME TrapFrame
);

NORETURN
void
MtpScheduleBlockedSyscall(
    IN PITHREAD Thread,
    IN MTSTATUS ReturnStatus
);

#endif
