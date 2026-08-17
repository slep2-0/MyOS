/*
    * PROJECT:      MatanelOS Kernel
    * LICENSE:      GPLv3
    * PURPOSE:      Scheduler Implementation.
    */

#include "../../includes/me.h"
#include "../../includes/mh.h"
#include "../../assert.h"

#include "../../includes/ps.h"
#include "../../includes/mg.h"
#include "../../includes/ob.h"
extern PROCESSOR cpus[];

// assembly stubs to save and restore register contexts.
extern void restore_context(PITHREAD Thread, PITHREAD PreviousThread);
extern void restore_user_context_to_user(PETHREAD thread, PITHREAD PreviousThread);
extern void restore_user_context_to_kernel(PETHREAD thread, PITHREAD PreviousThread);

// Idle thread, runs when no other is ready.
// Stack for idle thread
extern void kernel_idle_checks(void);
#define IDLE_STACK_SIZE 4096

extern EPROCESS PsInitialSystemProcess;

void InitScheduler(void)

/*++

    Routine description:

        Initializes scheduler queues and processor scheduling state.

    Arguments:

        None.

    Return Values:

        None.

--*/

{
    PETHREAD idleThread = NULL;
    MTSTATUS Status = ObCreateObject(PsThreadType, sizeof(ETHREAD), (void**)&idleThread);
    if (MT_FAILURE(Status)) {
        // If we can't allocate the idle thread during boot, the system is toast.
        MeBugCheckEx(MEMORY_LIMIT_REACHED, NULL, NULL, NULL, NULL);
    }

    MeGetCurrentProcessor()->idleThread = idleThread;

    // Use the unified helper to set up APC lists, PIDs, and states
    PspInitializeThread(idleThread, &PsInitialSystemProcess, (TimeSliceTicks)1); // 1ms timeslice

    // Idle thread specific overrides
    idleThread->TID = 0;
    idleThread->SystemThread = true;

    // Set up the execution context
    void* idleStack = MiCreateKernelStack(false);
    assert(idleStack != NULL);

    TRAP_FRAME cfm;
    kmemset(&cfm, 0, sizeof(cfm));
    // A C function entered through a synthetic restore must observe the same
    // ABI stack alignment as if a call instruction had entered it.
    cfm.rsp = (uint64_t)idleStack - 8;
    cfm.rip = (uint64_t)kernel_idle_checks;
    cfm.cs = KERNEL_CS;
    cfm.ss = KERNEL_SS;
    cfm.rflags = INITIAL_RFLAGS;

    idleThread->InternalThread.TrapRegisters = cfm;
    idleThread->InternalThread.StackBase = idleStack;
    idleThread->InternalThread.IsLargeStack = false;
    idleThread->InternalThread.KernelStack = idleStack;

    // Link to the System Process
    PsInitialSystemProcess.MainThread = idleThread;

    MsAcquirePushLockExclusive(&PsInitialSystemProcess.ThreadListLock);
    InsertHeadList(&PsInitialSystemProcess.AllThreads, &idleThread->ThreadListEntry);
    if (PsInitialSystemProcess.NumThreads == UINT32_MAX) {
        MeBugCheckEx(MEMORY_OVERFLOW_DETECTION, &PsInitialSystemProcess,
            idleThread, &PsInitialSystemProcess.AllThreads, RETADDR(0));
    }
    PsInitialSystemProcess.NumThreads++; // Maintain accurate thread count
    MsReleasePushLockExclusive(&PsInitialSystemProcess.ThreadListLock);

    // Reset Scheduler state
    // We do NOT call MeEnqueueThread here, the idle thread remains outside the ready queue.
    MeGetCurrentProcessor()->currentThread = NULL;
    MeGetCurrentProcessor()->readyQueue.head = MeGetCurrentProcessor()->readyQueue.tail = NULL;
}

// Enqueue the thread if it's still RUNNING.
static void enqueue_runnable(PITHREAD t)

/*++

    Routine description:

        Inserts a runnable thread into its processor ready queue.

    Arguments:

        [IN] t - Thread being queued.

    Return Values:

        None.

--*/

{
    if (t->ThreadState == THREAD_RUNNING) {
        t->ThreadState = THREAD_READY;
        t->TimeSlice = t->TimeSliceAllocated;
        MeEnqueueThreadWithLock(&MeGetCurrentProcessor()->readyQueue, PsGetEThreadFromIThread(t)); // Insert into CPU ready queue
    }
}

extern uint32_t g_cpuCount; // extern the global cpu count. (gotten from smp)
extern bool smpInitialized;

static
inline
bool
MepReadyQueueContainsThreadLocked(
    IN Queue* ReadyQueue,
    IN PETHREAD Thread
)

/*++

    Routine description:

        Reports whether a thread is present in a ready queue while its lock is held.

    Arguments:

        [IN] ReadyQueue - Ready queue examined while its lock is held.
        [IN] Thread - Thread affected by the operation.

    Return Values:

        A nonzero value when the reported condition holds, or zero otherwise.

--*/

{
    PETHREAD Current = ReadyQueue->head;

    while (Current != NULL) {
        if (Current == Thread) {
            return true;
        }

        PDOUBLY_LINKED_LIST NextEntry =
            Current->SchedulerListEntry.Flink;

        Current = NextEntry
            ? CONTAINING_RECORD(
                NextEntry,
                ETHREAD,
                SchedulerListEntry
            )
            : NULL;
    }

    return false;
}

bool
MepMigrateReadyThread(
    PETHREAD Thread,
    PPROCESSOR Source,
    PPROCESSOR Destination
)

/*++

    Routine description : 
    
        Performs a ready queue migration on the Destination CPU.

        This is used for load balancing, when 1 CPU readyQueue has too many threads on it, while other CPUs have much lesser.

    Arguments:

        Thread - The thread to migrate to Destination from Source
        Source - The source processor, that hosts the thread in its readyQueue
        Destination - The dedstination processor which will host the thread in its readyQueue

    Return Values:

        True if the thread was moved, false otherwise

--*/

{
    if (!Thread || !Source || !Destination) return false;

    // The source CPU must not be the destination CPU
    if (Source == Destination) return false;

    // Acquire both CPU locks in ascending PROCESSOR.ID order
    // Small boolean optimization to not calculate the exact same thing later
    bool AcquireDestinationFirst = false;
    bool Moved = false;

    IRQL prevIrql;
    if (Source->ID > Destination->ID) {
        AcquireDestinationFirst = true;

        MsAcquireSpinlock(&Destination->readyQueue.lock, &prevIrql);
        MsAcquireSpinlockAtDpcLevel(&Source->readyQueue.lock);
    }
    else {
        MsAcquireSpinlock(&Source->readyQueue.lock, &prevIrql);
        MsAcquireSpinlockAtDpcLevel(&Destination->readyQueue.lock);
    }

    // Verify that the Thread is actually linked in the Source ready queue
    if (!MepReadyQueueContainsThreadLocked(&Source->readyQueue, Thread)) goto Cleanup;

    // To migrate a thread it must not be running or blocking (or terminating).
    if (InterlockedLoadAcquire(&Thread->InternalThread.ThreadState) != THREAD_READY) goto Cleanup;

    // The thread must not be factually running inside of a processor.
    if (InterlockedLoadAcquire(
        &Thread->InternalThread.ActiveProcessor
    ) != NULL) {
        goto Cleanup;
    }

    // Thread is validated, remove it from Source queue and insert it in the Destination
    if (!MeRemoveThreadFromQueue(&Source->readyQueue, Thread)) {
        assert(false, "readyQueue corruption detected in the current CPU even though it was locked");
        goto Cleanup;
    }

    // Enqueue to dest
    MeEnqueueThread(&Destination->readyQueue, Thread);

    Moved = true;

Cleanup:
    if (AcquireDestinationFirst) {
        MsReleaseSpinlockFromDpcLevel(&Source->readyQueue.lock);
        MsReleaseSpinlock(&Destination->readyQueue.lock, prevIrql);
    }
    else {
        MsReleaseSpinlockFromDpcLevel(&Destination->readyQueue.lock);
        MsReleaseSpinlock(&Source->readyQueue.lock, prevIrql);
    }

    if (Moved) {
        InterlockedStoreRelease(&Destination->schedulePending, true);
    }

    return Moved;
}

// The following function uses CPU Work stealing to steal other CPUs thread (in a queue), if the current thread has no scheduled threads in the queue.
static PITHREAD MeAcquireNextScheduledThread(void)

/*++

    Routine description:

        Selects and removes the next runnable thread from a processor ready queue.

    Arguments:

        None.

    Return Values:

        A pointer to the resulting object, or NULL when no result is available.

--*/

{
    // First, lets try to get from our own queue.
    PETHREAD chosenThread = MeDequeueThreadWithLock(&MeGetCurrentProcessor()->readyQueue);
    if (chosenThread) return &chosenThread->InternalThread;

#ifndef MT_UP
    if (smpInitialized) {
        // Our own CPU queue is empty, steal from others.
        for (uint32_t i = 0; i < g_cpuCount; i++) {
            if (cpus[i].lapic_ID == MeGetCurrentProcessor()->lapic_ID) continue; // skip ourselves.

            // The reason I used the self pointer here, is because the BSP in the cpus array, is empty except for 4 fields, as its main struct is cpu0, 
            // which is defined at the kernel main, so we access it through self, view SMP.C prepare_percpu for more info.
            Queue* victimQueue = &cpus[i].self->readyQueue;

            IRQL prevIrql;
            MsAcquireSpinlock(&victimQueue->lock, &prevIrql);
            if (!victimQueue->head) {
                MsReleaseSpinlock(&victimQueue->lock, prevIrql);
                continue; // skip empty queues
            }
            MsReleaseSpinlock(&victimQueue->lock, prevIrql);

            chosenThread = MeDequeueThreadWithLock(victimQueue);
            if (!chosenThread) continue;

            // A non-NULL owner means the thread has a kernel stack associated
            // with another CPU. Until context switching has an explicit
            // switched-away handshake, only steal never-dispatched threads.
            if (InterlockedLoadAcquire(
                &chosenThread->InternalThread.ActiveProcessor
            ) != NULL) {
                MeEnqueueThreadWithLock(victimQueue, chosenThread);
                continue;
            }

            return &chosenThread->InternalThread;
        }
    }
#endif

    // No thread found.
    return NULL;
}

#ifdef DEBUG
FORCEINLINE
bool
MepIsCanonicalAddress(
    IN uintptr_t Address
)

/*++

    Routine description:

        Reports whether an address has a valid x64 canonical form.

    Arguments:

        [IN] Address - Virtual address affected by the operation.

    Return Values:

        A nonzero value when the reported condition holds, or zero otherwise.

--*/

{
    uintptr_t UpperBits = Address >> 48;
    uintptr_t SignExtension = ((Address >> 47) & 1) ? 0xFFFF : 0;

    return UpperBits == SignExtension;
}
#endif

void
MePrepareUserDispatchForReturn(
    PTRAP_FRAME TrapFrame
)

// Function will check if user exception is pending, if it is, it will prepare exception delivery for return
// If a user exception preperation fails, the function terminates the current thread
// If there is no user exception, the function will instead prepare for user APCs return, if there are any
// This is all checked on the current thread.

/*++

    Routine description:

        Prepares pending user APC or exception dispatch before returning to user mode.

    Arguments:

        [IN] TrapFrame - Saved processor state for the interrupted context.

    Return Values:

        None.

--*/

{
    PITHREAD Thread = MeGetCurrentThread();

    // Must return to user code only.
    if (!Thread || !TrapFrame || (TrapFrame->cs & 3) != 3) {
        return;
    }

    bool UserExceptionActive = InterlockedLoadAcquire(&Thread->UserExceptionActive);
    bool UserApcActive = Thread->UserApcActive;

    if (UserExceptionActive || UserApcActive) {
        // Already returning into a dispatcher. Never stack another one.
        return;
    }

    if (InterlockedLoadAcquire(&Thread->UserExceptionPending)) {
        MTSTATUS Status = ExpPrepareUserExceptionDispatch(TrapFrame);

        if (Status != MT_SUCCESS) {
            // Pending existed, so this is a real delivery failure.
            // Apply deterministic termination policy, we do not deliver an APC.
            PspExitThread(
                (MTSTATUS)Thread->PendingExceptionRecord.ExceptionCode
            );
        }

        // Success means the frame now enters MeUserExceptionDispatcher, trap frame has changed.
        return;
    }

    // No pending/active exception: APC delivery is allowed.
    MePrepareUserApcForReturn(TrapFrame);
}

NORETURN
void
Schedule(void)

/*++

    Routine description:

        Selects a runnable thread and switches the current processor to it.

    Arguments:

        None.

    Return Values:

        None.

--*/

{
    //gop_printf(COLOR_PURPLE, "**In scheduler, IRQL: %d**\n", MeGetCurrentIrql());
    IRQL oldIrql;
    MeRaiseIrql(DISPATCH_LEVEL, &oldIrql); // Prevents scheduling re-entrance.

    PPROCESSOR cpu = MeGetCurrentProcessor();
    PITHREAD current = MeGetCurrentProcessor()->currentThread;
    PITHREAD IdleThread = &MeGetCurrentProcessor()->idleThread->InternalThread;

    // Check if we need to delete another thread's (safe now, we are at a separate stack)
    if (cpu->ZombieThread) {
        // Drop the reference, we are on another thread's stack
        ObDereferenceObject((void*)cpu->ZombieThread);
        cpu->ZombieThread = NULL;
    }

    if (current && current != IdleThread &&
        current->ThreadState == THREAD_BLOCKING) {
        // Publish BLOCKED before checking completion. A concurrent wake either
        // changes BLOCKED to READY and queues us, or observes BLOCKING and
        // leaves this CPU to cancel the switch below.
        InterlockedStore(
            &current->ThreadState,
            THREAD_BLOCKED
        );

        // WaitStatus is claimed before queue cleanup. Waiting for the separate
        // completion publication prevents this CPU from returning from the
        // wait and reusing WaitBlock while the winning CPU still owns cleanup.
        if (InterlockedLoadAcquire(
                &current->WaitCompletionComplete
            )) {
            __sync_bool_compare_and_swap(
                &current->ThreadState,
                THREAD_BLOCKED,
                THREAD_RUNNING
            );
        }
    }

    // All thread's that weren't RUNNING are ignored by the Scheduler. (like BLOCKED threads when waiting or an event, ZOMBIE threads, TERMINATED, etc..)
    if (current && current != IdleThread && current->ThreadState == THREAD_TERMINATING) {
        cpu->ZombieThread = current;
        current = NULL;
    }
    else if (current && current != IdleThread && current->ThreadState == THREAD_RUNNING) {
        // The current thread's registers were already saved in isr_stub. (look after the pushes) (also saved in MtSleepCurrentThread)
        enqueue_runnable(current);
    }

    PITHREAD next = MeAcquireNextScheduledThread();

    // Save the current thread as the previous thread for setting his ActiveProcessor as NULL when its switched away.
    PITHREAD PreviousThread = cpu->currentThread;

    if (!next) {
        next = IdleThread;
    }

#ifdef DEBUG
    assert(IdleThread != NULL);
    assert(next != NULL);
    assert(next->KernelStack != NULL);
    assert(next == IdleThread || next->ThreadState == THREAD_READY,
        "Scheduler selected a thread that was not ready.");
    // A selected READY thread is normally unowned. The only valid exception
    // is selecting the outgoing thread again without changing CPUs.
    assert(
        next->ActiveProcessor == NULL ||
        (next == PreviousThread && next->ActiveProcessor == cpu),
        "Scheduler selected a thread whose stack is still owned."
    );

    uintptr_t StackTop = (uintptr_t)next->KernelStack;
    size_t StackSize = next->IsLargeStack
        ? MI_LARGE_STACK_SIZE
        : MI_STACK_SIZE;
    assert(StackTop >= StackSize);
    uintptr_t StackLimit = StackTop - StackSize;

    uintptr_t SavedRip = (uintptr_t)next->TrapRegisters.rip;
    uintptr_t SavedRsp = (uintptr_t)next->TrapRegisters.rsp;
    uint64_t SavedCs = next->TrapRegisters.cs;

    assert(SavedCs == KERNEL_CS || SavedCs == USER_CS,
        "Saved context has an invalid code selector.");
    assert(SavedRip != 0 && MepIsCanonicalAddress(SavedRip),
        "Saved context has a noncanonical RIP.");

    if (SavedCs == KERNEL_CS) {
        // Kernel stacks grow down from KernelStack. KernelStack itself is the
        // first byte above the allocation and is therefore an exclusive bound.
        assert(SavedRsp >= StackLimit && SavedRsp < StackTop,
            "Saved kernel RSP is outside the thread's kernel stack.");
    }
    else {
        assert(SavedRsp != 0 && MepIsCanonicalAddress(SavedRsp),
            "Saved user context has a noncanonical RSP.");
        assert(SavedRip <= MmHighestUserAddress &&
            SavedRsp <= MmHighestUserAddress,
            "Saved user context points outside user address space.");
        assert(next->TrapRegisters.ss == USER_SS,
            "Saved user context has an invalid stack selector.");
    }

#endif

    // A privilege-changing interrupt consults TSS.RSP0 before any entry stub
    // can run. Resolve shared APC state before the final interrupt-disabled
    // publication window so an IPI sender cannot wait on this CPU while this
    // CPU spins on a lock owned by the sender.
    IRQL apcQueueIrql;
    MsAcquireSpinlock(&next->ApcQueueLock, &apcQueueIrql);
    InterlockedStoreRelease(
        &next->ApcState.KernelApcPending,
        !IsListEmpty(&next->ApcState.ApcListHead[KernelMode])
    );
    InterlockedStoreRelease(
        &next->ApcState.UserApcPending,
        !IsListEmpty(&next->ApcState.ApcListHead[UserMode])
    );
    MsReleaseSpinlock(&next->ApcQueueLock, apcQueueIrql);

    // currentThread, TSS.RSP0, CR3, and the restored context describe one
    // selected thread. Publish them atomically against local interrupts.
    // Schedule never returns; the selected trap frame supplies the eventual
    // IF state.
    MeDisableInterrupts();
    
    // Save previous thread FS base
    if (PreviousThread != NULL &&
        PreviousThread != IdleThread &&
        !PsIsKernelThread(PsGetEThreadFromIThread(PreviousThread))) {
        PreviousThread->UserFsBase = __readmsr(IA32_FS_BASE);
    }

    next->ThreadState = THREAD_RUNNING;
    next->ActiveProcessor = cpu;
    assert(cpu->tss != NULL);
    ((TSS*)cpu->tss)->rsp0 = (uint64_t)next->KernelStack;
    cpu->currentThread = next;

    // Dispatch preparation can write an exception or APC frame to the
    // selected thread's user stack. Switch address spaces before that work;
    // otherwise a schedule from another process can publish the frame into
    // the outgoing process at the same virtual address.
    PEPROCESS ActiveProcess = next->ApcState.SavedApcProcess;
    assert(ActiveProcess != NULL);
    uintptr_t TargetCr3 =
        ActiveProcess->InternalProcess.PageDirectoryPhysical;
    assert(TargetCr3 != 0);
    if (__read_cr3() != TargetCr3) {
        __write_cr3(TargetCr3);
    }

    // Switch to the next thread user FS base.
    uint64_t NextFsBase = 0;

    if (next != IdleThread &&
        !PsIsKernelThread(PsGetEThreadFromIThread(next))) {
        NextFsBase = next->UserFsBase;
    }

#ifdef DEBUG
    assert(
        NextFsBase == 0 || MepIsCanonicalAddress(NextFsBase),
        "Selected thread has a noncanonical FS base."
    );
#endif

    __writemsr(IA32_FS_BASE, NextFsBase);

    // Lower IRQL back to its original value.
    MeLowerIrql(oldIrql);

    // Check any user exceptions and redirect execution to the mtdll exception handler if there are any.
    // Check any user apcs and request a software interrupt when returning to user mode if there are any.
    MePrepareUserDispatchForReturn(&next->TrapRegisters);

    if (PsIsKernelThread(PsGetEThreadFromIThread(next))) {
        restore_context(next, PreviousThread);
    }
    else {
        // Saved CS is the authoritative resume mode. Interrupt frames provide
        // it directly, and MsYieldExecution records KERNEL_CS for a blocked
        // syscall continuation. Address ranges are not execution-state.
        if ((next->TrapRegisters.cs & 3) == 0) {
            restore_user_context_to_kernel(PsGetEThreadFromIThread(next), PreviousThread);
        }
        else {
            restore_user_context_to_user(PsGetEThreadFromIThread(next), PreviousThread);
        }
    }

    UNREACHABLE_CODE();
}
