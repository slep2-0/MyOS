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

// TODO Proper SSDT with offsets to handlers from SSDT base instead of raw pointers (for security) - dont.
// Along with validating that the handler is in the .text section of the kernel - maybe.
SYSCALL_INIT_ENTRY SyscallTable[] = {
    // Syscalls are here.
    {.Num = 0, .Handler = MtAllocateVirtualMemory},
    {.Num = 1, .Handler = MtOpenProcess},
    {.Num = 2, .Handler = MtTerminateProcess},
    {.Num = 3, .Handler = MtReadFile},
    {.Num = 4, .Handler = MtWriteFile},
    {.Num = 5, .Handler = MtCreateFile},
    {.Num = 6, .Handler = MtClose},
    {.Num = 7, .Handler = MtTerminateThread},
    {.Num = 8, .Handler = MtQueryVirtualMemory},
    {.Num = 9, .Handler = MtProtectVirtualMemory},
    {.Num = 10, .Handler = MtFreeVirtualMemory},
    {.Num = 11, .Handler = MtCreateThread},
    {.Num = 12, .Handler = MtContinue},
    {.Num = 13, .Handler = MtDelayExecution},
    {.Num = 14, .Handler = MtWaitForSingleObject},
    {.Num = 15, .Handler = MtCreateEvent},
    {.Num = 16, .Handler = MtQueryEvent},
    {.Num = 17, .Handler = MtSetEvent},
    {.Num = 18, .Handler = MtResetEvent},
    {.Num = 19, .Handler = MtCreateMutex},
    {.Num = 20, .Handler = MtQueryMutex},
    {.Num = 21, .Handler = MtReleaseMutex},
    {.Num = 22, .Handler = MtCreateSemaphore},
    {.Num = 23, .Handler = MtQuerySemaphore},
    {.Num = 24, .Handler = MtReleaseSemaphore},
    {.Num = 25, .Handler = MtQueryInformationProcess},
    {.Num = 26, .Handler = MtQueryInformationThread},
    {.Num = 27, .Handler = MtSuspendThread},
    {.Num = 28, .Handler = MtResumeThread},
    {.Num = 29, .Handler = MtRaiseException},
    {.Num = 30, .Handler = MtCreateSection},
    {.Num = 31, .Handler = MtMapViewOfSection},
    {.Num = 32, .Handler = MtUnmapViewOfSection},
    {.Num = 255, .Handler = MtPrintConsole}
};

bool SyscallsAlreadyInitialized = false;

void
MtSetupSyscall(
    void
)

/*++

    Routine description:

        Initializes the SYSCALL MSRs and publishes the kernel system-service
        dispatch table.

    Arguments:

        None.

    Return Values:

        None.

--*/

{
    // Write the Code Segment selectors into the STAR msr.
    uint64_t STAR = ((uint64_t)KERNEL_CS << 32) | ((uint64_t)(USER_DS - 8) << 48);
    __writemsr(IA32_STAR, STAR);

    // Write the syscall entrypoint to LSTAR msr.
    __writemsr(IA32_LSTAR, (uint64_t)MtSyscallEntry);

    // Do not inherit single-step, interrupts, string direction, nested-task, or
    // supervisor-user-access state from an untrusted user RFLAGS value.
    __writemsr(IA32_FMASK,
        (1ULL << 8) | (1ULL << 9) | (1ULL << 10) |
        (1ULL << 14) | (1ULL << 18));

    // User-mode syscall/interrupt entry expects swapgs to reveal this CPU.
    __writemsr(IA32_KERNEL_GS_BASE, (uint64_t)MeGetCurrentProcessor());

    // Setup list of syscalls.
    if (!InterlockedFetch8((volatile int8_t*) & SyscallsAlreadyInitialized)) {
        for (size_t i = 0; i < sizeof(SyscallTable) / sizeof(SyscallTable[0]); i++) {
            Ssdt[SyscallTable[i].Num] = SyscallTable[i].Handler;
        }
        // BSP Should run this first, no need for interlocked.
        SyscallsAlreadyInitialized = true;
    }

    // Enable SysCallEnable (SCE) in EFER.
    uint64_t EFER = __readmsr(MSR_EFER);
    EFER |= 1; // EFER.SCE
    __writemsr(MSR_EFER, EFER);
}
