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
    IN PKERNEL_ROUTINE KernelRoutine,
    _In_Opt PRUNDOWN_ROUTINE RundownRoutine,
    _In_Opt PNORMAL_ROUTINE NormalRoutine,
    _In_Opt void* NormalContext
)

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

    // Initialize the APC Standard fields.
    Apc->ApcType = 0;
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
    assert(Apc->ApcMode == KernelMode || Apc->ApcMode == UserMode);

    if (Apc->ApcMode != KernelMode && Apc->ApcMode != UserMode) {
        return false;
    }

    bool Inserted = false;
    PITHREAD Thread = Apc->Thread;
    PPROCESSOR TargetProcessor = NULL;

    // Acquire the APC lock
    IRQL oldIrql;
    MsAcquireSpinlock(&Thread->ApcQueueLock, &oldIrql);

    // Double-check after acquiring the lock to prevent race conditions
    PETHREAD EThread = PsGetEThreadFromIThread(Thread);
    if (!Apc->Inserted &&
        InterlockedLoadAcquire(&EThread->TerminationState) ==
        ThreadTerminationNone) {
        Apc->SystemArgument1 = SystemArgument1;
        Apc->SystemArgument2 = SystemArgument2;
        Apc->Inserted = 1;

        // Queue the APC in the list for its delivery mode.
        InsertTailList(
            &Thread->ApcState.ApcListHead[Apc->ApcMode],
            &Apc->ApcListEntry
        );
        Inserted = true;

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
            TargetProcessor = Thread->ActiveProcessor;
        }
    }

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
{
#ifdef DEBUG
    gop_printf(COLOR_CYAN, "**In MeRetireAPCs**\n");
#endif
    PPROCESSOR cpu = MeGetCurrentProcessor();
    PITHREAD currentThread = cpu->currentThread;

    if (!currentThread) return;

    // A vector may be stale, and a second vector may already be pending while
    // the first one drains the queue. Treat either case as a harmless nudge.
    if (cpu->ApcRoutineActive) return;

    cpu->ApcRoutineActive = true;

    // Kernel APCs are always considered before user APCs.
    while (true) {
        IRQL oldIrql;
        MsAcquireSpinlock(&currentThread->ApcQueueLock, &oldIrql);

        PDOUBLY_LINKED_LIST KernelList =
            &currentThread->ApcState.ApcListHead[KernelMode];
        if (IsListEmpty(KernelList)) {
            InterlockedStoreRelease(
                &currentThread->ApcState.KernelApcPending,
                false
            );
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
        if (NormalRoutine) {
            MsAcquireSpinlock(&currentThread->ApcQueueLock, &oldIrql);
            currentThread->ApcState.KernelApcInProgress = true;
            MsReleaseSpinlock(&currentThread->ApcQueueLock, oldIrql);

            NormalRoutine(NormalContext, SysArg1, SysArg2);

            MsAcquireSpinlock(&currentThread->ApcQueueLock, &oldIrql);
            currentThread->ApcState.KernelApcInProgress = false;
            MsReleaseSpinlock(&currentThread->ApcQueueLock, oldIrql);
        }
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
        if (!TrapFrame || ((TrapFrame->cs & 3) != 3) ||
            currentThread->UserApcActive) {
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
        uint64_t Dispatcher = (uint64_t)PspFindMtdllEntryAddress(
            DISPATCHER_FUNC_NAME,
            PsGetEThreadFromIThread(currentThread)
        );

        if (!Dispatcher || Dispatcher > MmHighestUserAddress ||
            Trap->rsp <= (128 + sizeof(TRAP_FRAME) + 8)) {
            MmFreePool(Apc);
            cpu->ApcRoutineActive = false;
            PspExitThread(MT_APC_ERROR);
        }

        uint64_t UserStack = Trap->rsp - 128;
        UserStack -= sizeof(TRAP_FRAME);
        UserStack &= ~0x0FULL;

        try {
            kmemcpy((void*)UserStack, Trap, sizeof(TRAP_FRAME));
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
