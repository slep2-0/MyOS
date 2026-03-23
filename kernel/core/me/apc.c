/*++

Module Name:

    apc.c

Purpose:

    This translation unit contains the implementation of Asynchronous Procedure Calls in MatanelOS.

Author:

    slep (Matanel) 2026.

Revision History:

--*/

#include "../../includes/me.h"
#include "../../includes/ps.h"
#include "../../assert.h"

#define DISPATCHER_FUNC_NAME "MeUserApcDispatcher"

void
MeInitializeApc(
    IN PAPC Apc,
    IN struct _ITHREAD* TargetThread,
    IN PRIVILEGE_MODE ApcMode,
    IN void* KernelRoutine,
    _In_Opt void* RundownRoutine,
    _In_Opt void* NormalRoutine,
    _In_Opt void* NormalContext
)

{
    assert(NormalRoutine != NULL || NormalContext == NULL);
    // Initialize the APC Standard fields.
    Apc->Thread = TargetThread;
    Apc->ApcMode = ApcMode;
    Apc->NormalContext = NormalContext;

    // Initialize the APC routine fields.
    Apc->KernelRoutine = KernelRoutine;
    Apc->RundownRoutine = RundownRoutine;
    Apc->NormalContext = NormalRoutine;

    // Initialize the list head.
    InitializeListHead(&Apc->ApcListEntry);
}

bool
MeInsertQueueApc(
    IN PAPC Apc,
    IN void* SystemArgument1,
    IN void* SystemArgument2
)
{
    assert(MeGetCurrentIrql() <= DISPATCH_LEVEL);

    bool Inserted = false;
    PITHREAD Thread = Apc->Thread;

    if (Apc->Inserted) return Inserted;

    // Acquire the APC lock
    IRQL oldIrql;
    MsAcquireSpinlock(&Thread->ApcQueueLock, &oldIrql);

    // Double-check after acquiring the lock to prevent race conditions
    if (!Apc->Inserted) {
        Apc->SystemArgument1 = SystemArgument1;
        Apc->SystemArgument2 = SystemArgument2;
        Apc->Inserted = 1;

        // Queue the APC
        InsertTailList(&Thread->ApcListHead, &Apc->ApcListEntry);
        Inserted = true;

        // If the target thread is currently running on the current CPU, request an APC interrupt.
        PPROCESSOR cpu = MeGetCurrentProcessor();

        if (Thread->ThreadState == THREAD_RUNNING && Thread->ActiveProcessor != NULL) {
            if (cpu->currentThread == Thread) {
                cpu->ApcInterruptRequested = true;
            }
            else {
                // Set IPI to the next thread's CPU.
                IPI_PARAMS IpiParams = { 0 };
                MhSendActionToSpecificCpuAndWait(Thread->ActiveProcessor, CPU_ACTION_REQUEST_APC, IpiParams);
            }
        }
    }

    MsReleaseSpinlock(&Thread->ApcQueueLock, oldIrql);
    return Inserted;
}

void
MeRetireAPCs(
    void
)
{
#ifdef DEBUG
    gop_printf(COLOR_CYAN, "**In MeRetireAPCs**\n");
#endif
    PPROCESSOR cpu = MeGetCurrentProcessor();
    PITHREAD currentThread = cpu->currentThread;

    if (!currentThread) return;

    cpu->ApcRoutineActive = true;
    cpu->ApcInterruptRequested = false;

    while (true) {
        IRQL oldIrql;
        MsAcquireSpinlock(&currentThread->ApcQueueLock, &oldIrql);

        // If no APCs are queued just leave.
        if (currentThread->ApcListHead.Flink == &currentThread->ApcListHead) {
            MsReleaseSpinlock(&currentThread->ApcQueueLock, oldIrql);
            break;
        }

        // Dequeue the APC
        PDOUBLY_LINKED_LIST entry = RemoveHeadList(&currentThread->ApcListHead);
        PAPC Apc = CONTAINING_RECORD(entry, APC, ApcListEntry);
        Apc->Inserted = 0;

        MsReleaseSpinlock(&currentThread->ApcQueueLock, oldIrql);

        // Prepare context and arguments
        PNORMAL_ROUTINE NormalRoutine = Apc->NormalRoutine;
        void* NormalContext = Apc->NormalContext;
        void* SysArg1 = Apc->SystemArgument1;
        void* SysArg2 = Apc->SystemArgument2; 

        // KernelRoutine runs first and can alter the NormalRoutine or arguments
        if (Apc->KernelRoutine) {
            Apc->KernelRoutine(Apc, NormalRoutine, &NormalContext, &SysArg1, &SysArg2);
        }

        // If we have a NormalRoutine execute it.
        if (NormalRoutine) {
            if (Apc->ApcMode == KernelMode) {
                // Run Kernel-mode APC directly
                NormalRoutine(NormalContext, SysArg1, SysArg2);
            }
            else {
                // User mode APC, must change the trap frame and stuff.
                // Grab thread's TRAP_FRAME.
                PTRAP_FRAME Trap = &currentThread->TrapRegisters;

                // Make room for red zone.
                uint64_t UserStack = Trap->rsp - 128;

                // Make room for the TRAP_FRAME and align to 16 bytes
                UserStack -= sizeof(TRAP_FRAME);
                UserStack &= ~0x0FULL;

                // Push in the new trap frame.
                try {
                    kmemcpy((void*)UserStack, Trap, sizeof(TRAP_FRAME));
                } except{
                    continue;
                }
                end_try;

                // Set R8 To point to that location;
                Trap->r8 = UserStack;

                // Set RIP to MeUserApcDispatcher
                Trap->rip = (uint64_t)PspFindMtdllEntryAddress(DISPATCHER_FUNC_NAME, PsGetEThreadFromIThread(currentThread));

                if (!Trap->rip) {
                    assert(false, "Could not find MeUserApcDispatcher inside of MTDLL for APC.");
                    continue;
                }

                Trap->rdi = (uint64_t)NormalRoutine;
                Trap->rsi = (uint64_t)NormalContext;
                Trap->rdx = (uint64_t)SysArg1;
                Trap->rcx = (uint64_t)SysArg2;

                // Return immediately, we want this to be processed.
                // Note that the flag ApcRoutineActive stays on! Until MtContinue is called.
                // Free the APC and return.
                MmFreePool(Apc);
                return;
            }
        }
    }

    cpu->ApcRoutineActive = false;
}

bool
MeRemoveQueueApc(
    IN PAPC Apc
)
{
    PITHREAD Thread = Apc->Thread;
    bool Removed = false;
    IRQL oldIrql;

    // Acquire lock to modify list
    MsAcquireSpinlock(&Thread->ApcQueueLock, &oldIrql);

    if (Apc->Inserted) {
        // Unlink the entry from the thread's ApcListHead
        RemoveEntryList(&Apc->ApcListEntry);

        Apc->Inserted = 0;
        Removed = true;
    }

    MsReleaseSpinlock(&Thread->ApcQueueLock, oldIrql);

    return Removed;
}