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
#include "../../includes/exception.h"
#include "../../assert.h"

#define DISPATCHER_FUNC_NAME "MeUserApcDispatcher"

void
MeInitializeApc(
    IN PAPC Apc,
    IN struct _ITHREAD* TargetThread,
    IN PRIVILEGE_MODE ApcMode,
    IN PKERNEL_ROUTINE KernelRoutine,
    _In_Opt PRUNDOWN_ROUTINE RundownRoutine,
    _In_Opt PNORMAL_ROUTINE NormalRoutine,
    _In_Opt void* NormalContext
)

/*++

    Routine description:

        Initializes an APC object with its target thread, execution mode, and
        routine callbacks.

    Arguments:

        [OUT] Apc - The APC storage to initialize.
        [IN] TargetThread - The thread that receives the APC.
        [IN] ApcMode - KernelMode or UserMode delivery mode.
        [IN] KernelRoutine - The kernel callback used during APC dispatch.
        [IN OPTIONAL] RundownRoutine - The callback used if the APC is
        discarded during rundown.
        [IN OPTIONAL] NormalRoutine - The normal callback, if any.
        [IN OPTIONAL] NormalContext - The context passed to NormalRoutine.

    Return Values:

        None. Invalid required pointers cause a bugcheck and invalid routine
        combinations are rejected by assertions.

--*/

{
    if (!Apc || !TargetThread) {
        MeBugCheckEx(
            NULL_POINTER_DEREFERENCE,
            Apc,
            TargetThread,
            (void*)(uintptr_t)RETADDR(0),
            NULL
        );
    }

    // NormalContext is meaningful only when a NormalRoutine will receive it.
    assert(NormalRoutine != NULL || NormalContext == NULL);
    assert(KernelRoutine != NULL || NormalRoutine != NULL);

    // Initialize the APC Standard fields.
    Apc->Thread = TargetThread;
    Apc->ApcMode = ApcMode;
    Apc->Inserted = 0;
    Apc->NormalContext = NormalContext;
    Apc->SystemArgument1 = NULL;
    Apc->SystemArgument2 = NULL;

    // Initialize the APC routine fields.
    Apc->KernelRoutine = KernelRoutine;
    Apc->RundownRoutine = RundownRoutine;
    Apc->NormalRoutine = NormalRoutine;

    // Initialize the list entry.
    InitializeListHead(&Apc->ApcListEntry);
}

static
void
MepInsertApc(
    PDOUBLY_LINKED_LIST ApcList,
    PAPC Apc
)

/*++

    Routine description:

        Inserts an APC into the proper special or normal position of a locked APC queue.

    Arguments:

        [IN] ApcList - APC queue into which the APC is inserted.
        [IN] Apc - APC object associated with the callback.

    Return Values:

        None.

--*/

{
    /*
     * The caller must hold the APC-list lock.
     *
     * Queue invariant:
     *
     *     Special APCs -> Normal APCs
     *
     * FIFO ordering is preserved within each class.
     */

    const bool IsSpecialApc =
        Apc->ApcMode == KernelMode &&
        Apc->NormalRoutine == NULL;

    if (!IsSpecialApc)
    {
        // Normal APCs are always inserted after all existing APCs.
        InsertTailList(
            ApcList,
            &Apc->ApcListEntry
        );

        return;
    }

    assert(
        ApcList ==
        &Apc->Thread->ApcState.ApcListHead[KernelMode]
    );

    /*
     * Start at the tail and skip normal APCs until we find:
     *
     * The last queued special APC 
     * or
     * The list head if no special APC exists.
     */
    PDOUBLY_LINKED_LIST InsertionPoint = ApcList->Blink;

    while (InsertionPoint != ApcList)
    {
        PAPC QueuedApc = CONTAINING_RECORD(
            InsertionPoint,
            APC,
            ApcListEntry
        );

        const bool IsQueuedApcSpecial =
            QueuedApc->NormalRoutine == NULL;

        if (IsQueuedApcSpecial)
        {
            break;
        }

        InsertionPoint = InsertionPoint->Blink;
    }

    /*
     * Despite its name, InsertHeadList inserts immediately after the
     * supplied list entry. Therefore this inserts the APC after the
     * final special APC and before the first normal APC.
     */
    InsertHeadList(
        InsertionPoint,
        &Apc->ApcListEntry
    );
}

bool
MepInsertQueueApcLocked(
    IN PAPC Apc,
    IN void* SystemArgument1,
    IN void* SystemArgument2,
    OUT PPROCESSOR* TargetProcessor
)

/*++

    Routine description:

        Publishes an APC while the target thread APC-queue lock is held.

    Arguments:

        [IN] Apc - APC object associated with the callback.
        [IN] SystemArgument1 - First system argument supplied to the DPC.
        [IN] SystemArgument2 - Second system argument supplied to the DPC.
        [IN] TargetProcessor - Processor that should receive the request.

    Return Values:

        true when the APC is inserted, or false when it cannot be queued.

--*/

{
    assert(Apc != NULL);
    assert(Apc->Thread != NULL);
    assert(TargetProcessor != NULL);
    assert(Apc->ApcMode == KernelMode || Apc->ApcMode == UserMode);

    PITHREAD Thread = Apc->Thread;
    *TargetProcessor = NULL;

    /*
     * ApcQueueable and Inserted are tested while the same lock protects their
     * modification, so termination, removal, and insertion have one ordering.
     */
    if (Apc->Inserted ||
        InterlockedLoadAcquire(&Thread->ApcQueueable) == false) {
        return false;
    }

    Apc->SystemArgument1 = SystemArgument1;
    Apc->SystemArgument2 = SystemArgument2;
    Apc->Inserted = 1;

    MepInsertApc(
        &Thread->ApcState.ApcListHead[Apc->ApcMode],
        Apc
    );

    if (Apc->ApcMode == KernelMode) {
        InterlockedStoreRelease(
            &Thread->ApcState.KernelApcPending,
            true
        );
    }
    else {
        InterlockedStoreRelease(
            &Thread->ApcState.UserApcPending,
            true
        );
    }

    if (Thread->ThreadState == THREAD_RUNNING &&
        Thread->ActiveProcessor != NULL) {
        /*
         * The target is executing right now, so its CPU must be nudged after
         * ApcQueueLock is released. A non-running thread needs no interrupt:
         * the pending flag remains set until its next dispatch.
         */
        *TargetProcessor = Thread->ActiveProcessor;
    }

    return true;
}

bool
MeInsertQueueApc(
    IN PAPC Apc,
    IN void* SystemArgument1,
    IN void* SystemArgument2
)

/*++

    Routine description:

        Queues an APC to a thread and requests delivery on its active processor.

    Arguments:

        [IN] Apc - APC object associated with the callback.
        [IN] SystemArgument1 - First system argument supplied to the DPC.
        [IN] SystemArgument2 - Second system argument supplied to the DPC.

    Return Values:

        true when the APC is inserted, or false when insertion is rejected.

--*/

{
    assert(MeGetCurrentIrql() <= DISPATCH_LEVEL);
    assert(Apc->ApcMode == KernelMode || Apc->ApcMode == UserMode);

    if (Apc->ApcMode != KernelMode && Apc->ApcMode != UserMode) {
        return false;
    }

    bool Inserted = false;
    PITHREAD Thread = Apc->Thread;
    // CPU to nudge after queue publication, if the target is currently running.
    PPROCESSOR TargetProcessor = NULL;

    // Acquire the APC lock
    IRQL oldIrql;
    MsAcquireSpinlock(&Thread->ApcQueueLock, &oldIrql);

    Inserted = MepInsertQueueApcLocked(
        Apc,
        SystemArgument1,
        SystemArgument2,
        &TargetProcessor
    );

    MsReleaseSpinlock(&Thread->ApcQueueLock, oldIrql);

    // Never wait for a remote CPU while holding its thread's APC queue lock.
    if (Inserted && TargetProcessor != NULL) {
        PPROCESSOR CurrentProcessor = MeGetCurrentProcessor();
        if (TargetProcessor == CurrentProcessor) {
            bool InterruptsEnabled = MeDisableInterrupts();
            if (CurrentProcessor->currentThread == Thread) {
                MhRequestSoftwareInterrupt(APC_LEVEL);
            }
            MeEnableInterrupts(InterruptsEnabled);
        }
        else {
            IPI_PARAMS IpiParams = { 0 };
            MhSendActionToSpecificCpuAndWait(
                TargetProcessor,
                CPU_ACTION_REQUEST_APC,
                IpiParams
            );
        }
    }

    return Inserted;
}

void
MeRetireAPCs(
    IN PTRAP_FRAME TrapFrame
)

/*++

    Routine description:

        Retires deliverable kernel and user APCs queued to the current thread.

    Arguments:

        [IN] TrapFrame - Saved processor state for the interrupted context.

    Return Values:

        None.

--*/

{
#ifdef DEBUG
    gop_printf(COLOR_CYAN, "**In MeRetireAPCs**\n");
#endif
    PPROCESSOR cpu = MeGetCurrentProcessor();
    PITHREAD currentThread = cpu->currentThread;

    if (!currentThread) return;

    // A vector may be stale, and a second vector may already be pending while
    // the first one drains the queue. Treat either case as a harmless nudge.
    if (cpu->ApcRoutineActive) {
        return;
    }

    // A nonzero kernel special APC disable prohibits all APCs from being executed on this thread.
    // No need for interlocked or a spinlock, this is only modified by the current thread
    if (currentThread->SpecialApcDisable) return;

    cpu->ApcRoutineActive = true;

    // Kernel APCs are always considered before user APCs.
    while (true) {
        IRQL oldIrql;
        MsAcquireSpinlock(&currentThread->ApcQueueLock, &oldIrql);

        PDOUBLY_LINKED_LIST KernelList =
            &currentThread->ApcState.ApcListHead[KernelMode];

        // If the kernel list is empty, just break out of the kernel APC loop.
        if (IsListEmpty(KernelList)) {
            InterlockedStoreRelease(
                &currentThread->ApcState.KernelApcPending,
                false
            );
            MsReleaseSpinlock(&currentThread->ApcQueueLock, oldIrql);
            break;
        }

        // Before removing, see if its a kernel APC and we are allowed to run it
        // If its a special APC, we will run it.
        PDOUBLY_LINKED_LIST FirstEntry = KernelList->Flink;
        PAPC InspectionApc =
            CONTAINING_RECORD(FirstEntry, APC, ApcListEntry);

        bool NormalApcDisabled =
            currentThread->KernelApcDisable != 0 ||
            currentThread->ApcState.KernelApcInProgress;

        bool IsSpecial = MeIsSpecialApc(InspectionApc);

        // Check which type of APC.
        if (!IsSpecial && NormalApcDisabled) {
            // Kernel APC, we are not allowed to execute this
            // The neat thing is, since we ordered the APC Insertion by special APCs first, we dont have to iterate anymore
            // We can just break, since it is guranteed that there are no more special APCs in the list
            MsReleaseSpinlock(&currentThread->ApcQueueLock, oldIrql);
            break;
        }

        PDOUBLY_LINKED_LIST entry = RemoveHeadList(KernelList);
        PAPC Apc = CONTAINING_RECORD(entry, APC, ApcListEntry);
        Apc->Inserted = 0;
        InterlockedStoreRelease(
            &currentThread->ApcState.KernelApcPending,
            !IsListEmpty(KernelList)
        );

        MsReleaseSpinlock(&currentThread->ApcQueueLock, oldIrql);

        // Prepare context and arguments
        PNORMAL_ROUTINE NormalRoutine = Apc->NormalRoutine;
        void* NormalContext = Apc->NormalContext;
        void* SysArg1 = Apc->SystemArgument1;
        void* SysArg2 = Apc->SystemArgument2; 

        // KernelRoutine runs first and can alter the NormalRoutine or arguments
        if (Apc->KernelRoutine) {
            Apc->KernelRoutine(Apc, &NormalRoutine, &NormalContext, &SysArg1, &SysArg2);
        }

        // KernelApcInProgress describes a normal kernel APC, not the entire
        // software interrupt or a special APC's KernelRoutine.
        // NormalRoutines might touch user code and so must run in PASSIVE_LEVEL.
        if (!IsSpecial && NormalRoutine) {
            MsAcquireSpinlock(&currentThread->ApcQueueLock, &oldIrql);
            currentThread->ApcState.KernelApcInProgress = true;
            MsReleaseSpinlock(&currentThread->ApcQueueLock, oldIrql);

            // Let more special APCs execute while we run the normal routines
            cpu->ApcRoutineActive = false;

            MeLowerIrql(PASSIVE_LEVEL);
            NormalRoutine(NormalContext, SysArg1, SysArg2);

            IRQL PrevIrql;
            MeRaiseIrql(APC_LEVEL, &PrevIrql);

            // Normal routine came back (remember, it is preemptable)
            // Refresh CPU Ptr.
            cpu = MeGetCurrentProcessor();

            // The normal routine must not change the IRQL without changing it back at its exit.
            if (PrevIrql != PASSIVE_LEVEL) {
                MeBugCheckEx(
                    FATAL_IRQL_CORRUPTION,
                    Apc,
                    NormalRoutine,
                    currentThread,
                    NULL
                );
            }

            MsAcquireSpinlock(&currentThread->ApcQueueLock, &oldIrql);
            currentThread->ApcState.KernelApcInProgress = false;
            MsReleaseSpinlock(&currentThread->ApcQueueLock, oldIrql);

            cpu->ApcRoutineActive = true;
        }
    }

    // First check if kernel APCs are disabled or not (this disables user apcs too)
    if (currentThread->KernelApcDisable) {
        cpu->ApcRoutineActive = false;
        return;
    }

    // User APCs need a complete frame that returns to CPL3. Leave them pending
    // when this vector interrupted kernel mode.
    while (true) {
        IRQL oldIrql;
        MsAcquireSpinlock(&currentThread->ApcQueueLock, &oldIrql);

        PDOUBLY_LINKED_LIST UserList =
            &currentThread->ApcState.ApcListHead[UserMode];
        if (IsListEmpty(UserList)) {
            InterlockedStoreRelease(
                &currentThread->ApcState.UserApcPending,
                false
            );
            MsReleaseSpinlock(&currentThread->ApcQueueLock, oldIrql);
            break;
        }

        InterlockedStoreRelease(
            &currentThread->ApcState.UserApcPending,
            true
        );

        // We also must not return when exceptions are active too.
        if (!TrapFrame || ((TrapFrame->cs & 3) != 3) ||
            currentThread->UserApcActive ||
            InterlockedLoadAcquire(&currentThread->UserExceptionActive)) {
            MsReleaseSpinlock(&currentThread->ApcQueueLock, oldIrql);
            break;
        }

        PDOUBLY_LINKED_LIST entry = RemoveHeadList(UserList);
        PAPC Apc = CONTAINING_RECORD(entry, APC, ApcListEntry);
        Apc->Inserted = 0;
        InterlockedStoreRelease(
            &currentThread->ApcState.UserApcPending,
            !IsListEmpty(UserList)
        );
        MsReleaseSpinlock(&currentThread->ApcQueueLock, oldIrql);

        PNORMAL_ROUTINE NormalRoutine = Apc->NormalRoutine;
        void* NormalContext = Apc->NormalContext;
        void* SysArg1 = Apc->SystemArgument1;
        void* SysArg2 = Apc->SystemArgument2;

        if (Apc->KernelRoutine) {
            Apc->KernelRoutine(
                Apc,
                &NormalRoutine,
                &NormalContext,
                &SysArg1,
                &SysArg2
            );
        }

        if (!NormalRoutine) continue;

        PTRAP_FRAME Trap = TrapFrame;
        CONTEXT ContextRecord;
        ExpCaptureContextFromTrapFrame(Trap, &ContextRecord);

        uint64_t Dispatcher = (uint64_t)PspFindMtdllEntryAddress(
            DISPATCHER_FUNC_NAME,
            PsGetEThreadFromIThread(currentThread)
        );

        if (!Dispatcher || Dispatcher > MmHighestUserAddress ||
            !MI_IS_CANONICAL_ADDR(Trap->rsp) ||
            Trap->rsp > MmHighestUserAddress ||
            Trap->rsp <= (128 + sizeof(CONTEXT) + 15 + 8)) {
            MmFreePool(Apc);
            cpu->ApcRoutineActive = false;
            PspExitThread(MT_APC_ERROR);
        }

        uint64_t UserStack = Trap->rsp - 128;
        UserStack -= sizeof(CONTEXT);
        UserStack &= ~0x0FULL;

        try {
            kmemcpy(
                (void*)UserStack,
                &ContextRecord,
                sizeof(ContextRecord)
            );
        } except{
            MmFreePool(Apc);
            cpu->ApcRoutineActive = false;
            PspExitThread(MT_APC_ERROR);
        }
        end_try;

        Trap->r8 = UserStack;
        Trap->rsp = UserStack - 8;
        Trap->rip = Dispatcher;
        Trap->rdi = (uint64_t)NormalRoutine;
        Trap->rsi = (uint64_t)NormalContext;
        Trap->rdx = (uint64_t)SysArg1;
        Trap->rcx = (uint64_t)SysArg2;

        currentThread->UserApcActive = true;
        cpu->ApcRoutineActive = false;
        MmFreePool(Apc);
        return;
    }

    cpu->ApcRoutineActive = false;
}

void
MeRetireApcsOnSyscallExit(
    IN PTRAP_FRAME SyscallFrame
)

/*++

    Routine description:

        Retires pending APCs before a system call returns to its caller.

    Arguments:

        [IN] SyscallFrame - Saved user context for the system-call return path.

    Return Values:

        None.

--*/

{
    if (!SyscallFrame) return;

    PPROCESSOR Cpu = MeGetCurrentProcessor();
    PITHREAD Thread = Cpu->currentThread;
    if (!Thread || Cpu->ApcRoutineActive) return;

    IRQL QueueIrql;
    MsAcquireSpinlock(&Thread->ApcQueueLock, &QueueIrql);
    PDOUBLY_LINKED_LIST UserList =
        &Thread->ApcState.ApcListHead[UserMode];
    bool HasQueuedApcs = !IsListEmpty(UserList);
    InterlockedStoreRelease(
        &Thread->ApcState.UserApcPending,
        HasQueuedApcs
    );
    MsReleaseSpinlock(&Thread->ApcQueueLock, QueueIrql);
    if (!HasQueuedApcs) return;

    // SYSCALL saves only the 15 GPRs followed by user RSP. RCX and R11 carry
    // the architectural return RIP/RFLAGS. Expand that compact layout into a
    // complete CPL3 frame so user APC injection has real CS/SS/RSP fields.
    TRAP_FRAME UserFrame;
    kmemset(&UserFrame, 0, sizeof(UserFrame));
    kmemcpy(&UserFrame, SyscallFrame, FIELD_OFFSET(TRAP_FRAME, vector));
    UserFrame.rip = SyscallFrame->rcx;
    UserFrame.cs = USER_CS;
    UserFrame.rflags = SyscallFrame->r11;
    UserFrame.rsp = SyscallFrame->vector;
    UserFrame.ss = USER_SS;

    bool InterruptsEnabled = MeDisableInterrupts();
    IRQL OldIrql;
    MeRaiseIrql(APC_LEVEL, &OldIrql);

    // CR8 masks another APC vector while normal kernel APC routines retain the
    // interrupt state expected by the regular interrupt-delivery path.
    MeEnableInterrupts(InterruptsEnabled);
    MeRetireAPCs(&UserFrame);
    MeDisableInterrupts();
    MeLowerIrql(OldIrql);
    MeEnableInterrupts(InterruptsEnabled);

    kmemcpy(SyscallFrame, &UserFrame, FIELD_OFFSET(TRAP_FRAME, vector));
    SyscallFrame->rcx = UserFrame.rip;
    SyscallFrame->r11 = UserFrame.rflags;
    SyscallFrame->vector = UserFrame.rsp;
}

bool
MeRemoveQueueApc(
    IN PAPC Apc
)

/*++

    Routine description:

        Removes a queued APC before it begins delivery.

    Arguments:

        [IN] Apc - APC object associated with the callback.

    Return Values:

        true when the APC is removed, or false when it was not queued.

--*/

{
    PITHREAD Thread = Apc->Thread;
    bool Removed = false;
    assert(Apc->ApcMode == KernelMode || Apc->ApcMode == UserMode);

    if (Apc->ApcMode != KernelMode && Apc->ApcMode != UserMode) {
        return false;
    }

    IRQL oldIrql;

    // Acquire lock to modify list
    MsAcquireSpinlock(&Thread->ApcQueueLock, &oldIrql);

    if (Apc->Inserted) {
        // Unlink the entry from the thread's ApcListHead
        RemoveEntryList(&Apc->ApcListEntry);

        Apc->Inserted = 0;
        bool QueueNotEmpty = !IsListEmpty(
            &Thread->ApcState.ApcListHead[Apc->ApcMode]
        );
        if (Apc->ApcMode == KernelMode) {
            InterlockedStoreRelease(
                &Thread->ApcState.KernelApcPending,
                QueueNotEmpty
            );
        }
        else {
            InterlockedStoreRelease(
                &Thread->ApcState.UserApcPending,
                QueueNotEmpty
            );
        }
        Removed = true;
    }

    MsReleaseSpinlock(&Thread->ApcQueueLock, oldIrql);

    return Removed;
}

void
MiCheckForKernelApcDelivery(
    void
)

/*

    Routine Description:

        This function checks to detemine if a kernel APC can be delivered
        immediately to the current thread or a kernel APC interrupt should
        be requested. On entry to this routine the following conditions are
        true:

        1. Special kernel APCs are enabled for the current thread.

        2. Normal kernel APCs may also be enabled for the current thread.

        3. The kernel APC queue is not empty.

    Arguments:

        None

    Return Value:

        None

    Notes:

        This routine is ONLY called by kernel code that leaves a guarded
        or critcial region.
*/

{
    // If the IRQL is passive level, then the kernel APCs can be flushed immediately
    InterlockedStoreRelease(&MeGetCurrentThread()->ApcState.KernelApcPending, true);
    bool InterruptsEnabled = MeDisableInterrupts();
    MhRequestSoftwareInterrupt(APC_LEVEL);
    MeEnableInterrupts(InterruptsEnabled);
}

void
MePrepareUserApcForReturn(
    PTRAP_FRAME ReturnFrame
)

/*

    Routine Description:

        This function checks if user APCs are queued and so requests an interrupt for them, so when return for user code happens the APCs will execute.

    Arguments:

        ReturnFrame - Pointer to TRAP_FRAME the thread will return to user mode code to.

    Return Value:

        None
*/

{
    bool InterruptsEnabled = MeDisableInterrupts();
    assert(InterruptsEnabled == false);
    (void)InterruptsEnabled;

    PITHREAD Thread = MeGetCurrentThread();

    if (!Thread || !ReturnFrame) {
        return;
    }

    if ((ReturnFrame->cs & 3) != 3) {
        return; // We must return to user code only.
    }

    if (Thread->UserApcActive || InterlockedLoadAcquire(&Thread->UserExceptionActive)) {
        return; // Already executing a user apc, or, a user exception is active.
    }

    assert(Thread->KernelApcDisable == 0 && Thread->SpecialApcDisable == 0);

    if (InterlockedLoadAcquire(&Thread->ApcState.UserApcPending)) {
        MhRequestSoftwareInterrupt(APC_LEVEL);
    }
}
