/*++

Module Name:

    syscall.c

Purpose:

    This module contains the implementation of syscall setup in x86.

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "../../includes/mt.h"
#include "../../intrinsics/intrin.h"
#include "../../includes/mm.h"
#include "../../includes/ps.h"

// From syscall.asm
extern void MtSyscallEntry(void);

// The SSDT
SyscallHandler Ssdt[MAX_SYSCALLS];

typedef struct {
    uint8_t Num;
    void* Handler;
} SYSCALL_INIT_ENTRY;

// TODO Proper SSDT with offsets to handlers from SSDT base instead of raw pointers (for security)
// Along with validating that the handler is in the .text section
// and idk implement patchguard on the way
// patchguard works by queuing DPCs and KTIMERs, not by making a system thread
// (so its always hidden), honestly microsoft engineers are brilliant.
SYSCALL_INIT_ENTRY SyscallTable[] = {
    // Syscalls are here.
    {.Num = 0, .Handler = MtAllocateVirtualMemory},
    {.Num = 1, .Handler = MtOpenProcess},
    {.Num = 2, .Handler = MtTerminateProcess}
};

void
MtSetupSyscall(
    void
)

{
    // Write the Code Segment selectors into the STAR msr.
    uint64_t STAR = ((uint64_t)KERNEL_CS << 32) | ((uint64_t)(USER_DS - 8) << 48);
    __writemsr(IA32_STAR, STAR);

    // Write the syscall entrypoint to LSTAR msr.
    __writemsr(IA32_LSTAR, (uint64_t)MtSyscallEntry);

    // Write the FMASK (flag mask) MSR to flag IF and TF.
    __writemsr(IA32_FMASK, (1 << 8) | (1 << 9));

    // Write the current processor to IA32_KERNEL_GS_BASE for swapgs
    __writemsr(IA32_KERNEL_GS_BASE, 0);

    // Setup list of syscalls.
    for (size_t i = 0; i < sizeof(SyscallTable) / sizeof(SyscallTable[0]); i++) {
        Ssdt[SyscallTable[i].Num] = SyscallTable[i].Handler;
    }

    // Enable SysCallEnable (SCE) in EFER.
    uint64_t EFER = __readmsr(MSR_EFER);
    EFER |= 1; // EFER.SCE
    __writemsr(MSR_EFER, EFER);
}
