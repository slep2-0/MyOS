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

    bool Inserted = false;
    PITHREAD Thread = Apc->Thread;
    PPROCESSOR TargetProcessor = NULL;

    // Acquire the APC lock
    IRQL oldIrql;
    MsAcquireSpinlock(&Thread->ApcQueueLock, &oldIrql);

    // Double-check after acquiring the lock to prevent race conditions
    PETHREAD EThread = PsGetEThreadFromIThread(Thread);
    if (!Apc->Inserted &&
        __atomic_load_n(&EThread->TerminationState, __ATOMIC_ACQUIRE) ==
            ThreadTerminationNone) {
        Apc->SystemArgument1 = SystemArgument1;
        Apc->SystemArgument2 = SystemArgument2;
        Apc->Inserted = 1;

        // Queue the APC
        InsertTailList(&Thread->ApcListHead, &Apc->ApcListEntry);
        Inserted = true;

        if (Thread->ThreadState == THREAD_RUNNING && Thread->ActiveProcessor != NULL) {
            TargetProcessor = Thread->ActiveProcessor;
        }
    }

    MsReleaseSpinlock(&Thread->ApcQueueLock, oldIrql);

    // Never wait for a synchronous IPI while holding the APC queue lock. The
    // target scheduler also takes this lock with interrupts disabled.
    if (Inserted && TargetProcessor != NULL) {
        PPROCESSOR CurrentProcessor = MeGetCurrentProcessor();
        if (TargetProcessor == CurrentProcessor) {
            CurrentProcessor->ApcInterruptRequested = true;
        }
        else {
            IPI_PARAMS IpiParams = { 0 };
            MhSendActionToSpecificCpuAndWait(TargetProcessor,
                CPU_ACTION_REQUEST_APC, IpiParams);
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

    assert(cpu->ApcInterruptRequested == true);
    assert(cpu->ApcRoutineActive == false);

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

        PAPC QueuedApc = CONTAINING_RECORD(currentThread->ApcListHead.Flink,
            APC, ApcListEntry);
        if (QueuedApc->ApcMode == UserMode &&
            (!TrapFrame || ((TrapFrame->cs & 3) != 3) || currentThread->UserApcActive)) {
            // User APCs are injected only into a complete CPL3 return frame.
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
            Apc->KernelRoutine(Apc, &NormalRoutine, &NormalContext, &SysArg1, &SysArg2);
        }

        // If we have a NormalRoutine execute it.
        if (NormalRoutine) {
            if (Apc->ApcMode == KernelMode) {
                // Run Kernel-mode APC directly
                NormalRoutine(NormalContext, SysArg1, SysArg2);
            }
            else {
                // User-mode APCs must alter the live interrupt return frame.
                PTRAP_FRAME Trap = TrapFrame;
                uint64_t Dispatcher = (uint64_t)PspFindMtdllEntryAddress(
                    DISPATCHER_FUNC_NAME, PsGetEThreadFromIThread(currentThread));

                if (!Dispatcher || Dispatcher > MmHighestUserAddress ||
                    Trap->rsp <= (128 + sizeof(TRAP_FRAME) + 8)) {
                    MmFreePool(Apc);
                    cpu->ApcRoutineActive = false;
                    PspExitThread(MT_APC_ERROR);
                }

                // Make room for red zone.
                uint64_t UserStack = Trap->rsp - 128;

                // Make room for the TRAP_FRAME and align to 16 bytes
                UserStack -= sizeof(TRAP_FRAME);
                UserStack &= ~0x0FULL;

                // Push in the new trap frame.
                try {
                    kmemcpy((void*)UserStack, Trap, sizeof(TRAP_FRAME));
                } except{
                    MmFreePool(Apc);
                    cpu->ApcRoutineActive = false;
                    PspExitThread(MT_APC_ERROR);
                }
                end_try;

                // Set R8 To point to that location;
                Trap->r8 = UserStack;
                Trap->rsp = UserStack - 8;

                // Set RIP to MeUserApcDispatcher
                Trap->rip = Dispatcher;

                Trap->rdi = (uint64_t)NormalRoutine;
                Trap->rsi = (uint64_t)NormalContext;
                Trap->rdx = (uint64_t)SysArg1;
                Trap->rcx = (uint64_t)SysArg2;

                // CPU activity ends with this ISR; the outstanding user APC
                // belongs to the thread and follows it across migrations.
                currentThread->UserApcActive = true;
                cpu->ApcRoutineActive = false;
                MmFreePool(Apc);
                return;
            }
        }
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
    bool HasQueuedApcs = !IsListEmpty(&Thread->ApcListHead);
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
    Cpu->ApcInterruptRequested = true;

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
