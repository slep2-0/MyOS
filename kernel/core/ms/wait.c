#include "../../includes/ms.h"
#include "../../includes/me.h"
#include "../../includes/ps.h"
#include "../../includes/mg.h"
#include "../../assert.h"

SPINLOCK MsTimerQueueLock;
DOUBLY_LINKED_LIST MsTimerQueue;

static
bool
MspIsTimerQueued(
    IN PITHREAD Thread
)
{
    PDOUBLY_LINKED_LIST Entry = &Thread->WaitBlock.TimerListEntry;

    return Entry->Flink != NULL &&
        Entry->Blink != NULL &&
        Entry->Flink != Entry &&
        Entry->Blink != Entry;
}

static
void
MspInsertTimerQueueLocked(
    IN PITHREAD Thread
)
{
    PDOUBLY_LINKED_LIST Entry = &Thread->WaitBlock.TimerListEntry;

    if (MspIsTimerQueued(Thread)) {
        RemoveEntryList(Entry);
    }

    InitializeListHead(Entry);

    if (IsListEmpty(&MsTimerQueue)) {
        InsertTailList(&MsTimerQueue, Entry);
        return;
    }

    PDOUBLY_LINKED_LIST Current = MsTimerQueue.Flink;
    while (Current != &MsTimerQueue) {
        PITHREAD Block = CONTAINING_RECORD(Current, ITHREAD, WaitBlock.TimerListEntry);

        if (Thread->WaitBlock.WakeupTime < Block->WaitBlock.WakeupTime) {
            Entry->Flink = Current;
            Entry->Blink = Current->Blink;
            Current->Blink->Flink = Entry;
            Current->Blink = Entry;
            return;
        }

        Current = Current->Flink;
    }

    InsertTailList(&MsTimerQueue, Entry);
}

void
MsInsertTimerQueue(
    IN PITHREAD Thread,
    IN uint64_t WakeupTime
)
{
    IRQL OldIrql;

    Thread->WaitBlock.WakeupTime = WakeupTime;

    MeRaiseIrql(CLOCK_LEVEL, &OldIrql);
    MsAcquireSpinlockAtDpcLevel(&MsTimerQueueLock);
    MspInsertTimerQueueLocked(Thread);
    MsReleaseSpinlockFromDpcLevel(&MsTimerQueueLock);
    MeLowerIrql(OldIrql);
}

bool
MsRemoveTimerQueue(
    IN PITHREAD Thread
)
{
    IRQL OldIrql;
    bool Removed = false;

    MeRaiseIrql(CLOCK_LEVEL, &OldIrql);
    MsAcquireSpinlockAtDpcLevel(&MsTimerQueueLock);

    if (MspIsTimerQueued(Thread)) {
        RemoveEntryList(&Thread->WaitBlock.TimerListEntry);
        InitializeListHead(&Thread->WaitBlock.TimerListEntry);
        Removed = true;
    }

    MsReleaseSpinlockFromDpcLevel(&MsTimerQueueLock);
    MeLowerIrql(OldIrql);
    return Removed;
}

bool
MsClaimThreadWait(
    IN PITHREAD Thread,
    IN MTSTATUS CompletionStatus
)
{
    return __sync_val_compare_and_swap(
        &Thread->WaitStatus,
        MT_PENDING,
        CompletionStatus
    ) == MT_PENDING;
}

void
MsCompleteThreadWait(
    IN PITHREAD Thread
)
{
    // Completion is legal only after both queue registrations have been removed.
    assert(IsListEmpty(&Thread->WaitBlock.ObjectListEntry));
    assert(IsListEmpty(&Thread->WaitBlock.TimerListEntry));
    assert(Thread->WaitStatus != MT_PENDING);

    // THREAD_BLOCKING still owns a live kernel stack on its current CPU. The
    // scheduler observes the completed WaitStatus and makes it runnable as
    // part of the switch-away transaction.
    // See the THREAD_BLOCKING handshake in the scheduler switch-away path.
    if (__sync_val_compare_and_swap(
        &Thread->ThreadState,
        THREAD_BLOCKED,
        THREAD_READY
    ) != THREAD_BLOCKED) {
        return;
    }

    PPROCESSOR TargetProcessor = InterlockedLoadAcquire(
        &Thread->ActiveProcessor
    );

    if (!TargetProcessor) {
        TargetProcessor = MeGetCurrentProcessor();
    }

    MeEnqueueThreadWithLock(
        &TargetProcessor->readyQueue,
        PsGetEThreadFromIThread(Thread)
    );
    TargetProcessor->schedulePending = true;
}

// Does not acquire lock.
PITHREAD
GetHeadOfTimerQueue(void)
{
    PITHREAD Head = NULL;
    if (!IsListEmpty(&MsTimerQueue)) { Head = CONTAINING_RECORD(MsTimerQueue.Flink, ITHREAD, WaitBlock.TimerListEntry); }
    return Head;
}

void TimerExpirationDPC(DPC* Dpc, void* Context, void* SysArg1, void* SysArg2) {
    UNREFERENCED_PARAMETER(Dpc); UNREFERENCED_PARAMETER(Context); UNREFERENCED_PARAMETER(SysArg1); UNREFERENCED_PARAMETER(SysArg2);
    
    // Raise IRQL to CLOCK_LEVEL so we dont get preempted here.
    // (tpr)
    IRQL oldTimerIrql;
    MeRaiseIrql(CLOCK_LEVEL, &oldTimerIrql);

    // Acquire lock.
    MsAcquireSpinlockAtDpcLevel(&MsTimerQueueLock);

    while (!IsListEmpty(&MsTimerQueue)) {
        PITHREAD Thread = GetHeadOfTimerQueue();

        if (!Thread || Thread->WaitBlock.WakeupTime > MeSystemTickCount) {
            break;
        }

        RemoveEntryList(&Thread->WaitBlock.TimerListEntry);
        InitializeListHead(&Thread->WaitBlock.TimerListEntry);

        // Claim Thread WaitStatus atomically
        if (MsClaimThreadWait(Thread, MT_TIMEOUT)) {
            WAIT_REASON WaitReason = Thread->WaitBlock.WaitReason;
            PDISPATCHER_HEADER WaitObject = Thread->WaitBlock.Object;

            // Drop the timer lock and CLOCK_LEVEL before acquiring the dispatcher lock.
            MsReleaseSpinlockFromDpcLevel(&MsTimerQueueLock);
            MeLowerIrql(oldTimerIrql);

            // Dispatcher object
            if (WaitReason == WaitReasonDispatcherObject) {
                // Acquire Dispatcher Object Lock
                IRQL dispatcherIrql;
                MsAcquireSpinlock(&WaitObject->Lock, &dispatcherIrql);

                // WaitStatus is already claimed, but the wait block must still
                // identify this object until its protected list entry is removed.
                if (Thread->WaitBlock.WaitReason != WaitReasonDispatcherObject || Thread->WaitBlock.Object != WaitObject) {
                    MeBugCheckEx(
                        WAIT_STATE_FAILURE,
                        Thread,
                        (void*)(uintptr_t)Thread->WaitBlock.WaitReason,
                        Thread->WaitBlock.Object,
                        WaitObject
                    );
                }

                // Unlink this wait from the dispatcher object's wait list.
                PDOUBLY_LINKED_LIST Entry = &Thread->WaitBlock.ObjectListEntry;

                if (!IsListEmpty(Entry)) {
                    // A self-linked entry means another winning path already removed it.
                    RemoveEntryList(Entry);
                    InitializeListHead(Entry);
                }

                MsReleaseSpinlock(&WaitObject->Lock, dispatcherIrql);
            }
            else if (WaitReason == WaitReasonSleep) {
                if (WaitObject != NULL) {
                    MeBugCheckEx(
                        WAIT_STATE_FAILURE,
                        Thread,
                        (void*)(uintptr_t)WaitReason,
                        WaitObject,
                        (void*)TimerExpirationDPC
                    );
                }
            }
            else {
                MeBugCheckEx(
                    WAIT_STATE_FAILURE,
                    Thread,
                    (void*)(uintptr_t)WaitReason,
                    WaitObject,
                    (void*)TimerExpirationDPC
                );
            }

            MeClearWaitBlock(Thread);
            MsCompleteThreadWait(Thread);

            // Re-acquire the timer queue lock at CLOCK_LEVEL
            MeRaiseIrql(CLOCK_LEVEL, &oldTimerIrql);
            MsAcquireSpinlockAtDpcLevel(&MsTimerQueueLock);
        }
    }

    MsReleaseSpinlockFromDpcLevel(&MsTimerQueueLock);
    MeLowerIrql(oldTimerIrql);
}

static
bool
MspCanSatisfyDispatcherObject(
    IN PDISPATCHER_HEADER Header,
    IN PETHREAD Thread
)

/*++

    Routine Description:

        Checks if the given dispatcher object type from the header can satisfy the current thread's wait immediately without sleeping.

    Arguments:

        Header - Pointer to dispatcher header object to check satisfaction for.
        Thread - Pointer to thread that the dispatcher object relies on to check satisfaction (mutex).


    Return Value:

        Boolean value indicating if we can immediately satisfy the wait or not.

--*/

{
    switch (Header->Type) {
    case DispatcherNotificationEvent:
    case DispatcherSynchronizationEvent:
    case DispatcherSemaphore:
    case DispatcherThread:
    case DispatcherProcess:
        // For events, semaphores, threads, and processes, a positive signal
        // state means this wait can be satisfied without parking the thread.
        return Header->SignalState > 0;

    case DispatcherMutex: {
        // If the mutex is available to be taken OR, the owner thread wanted to re-acquire (recursive)
        // We can satisfy the wait and return.
        PMUTEX Mutex = (PMUTEX)Header;
        return (Header->SignalState > 0 || Mutex->OwnerThread == Thread);
    }
    default:
        MeBugCheckEx(
            WRONG_DISPATCHER_HEADER,
            Header,
            Thread,
            NULL,
            NULL
        );
    }
}

static
MTSTATUS
MspSatisfyDispatcherObject(
    IN PDISPATCHER_HEADER Header,
    IN PETHREAD Thread
)

{
    switch (Header->Type) {
    case DispatcherNotificationEvent:
    case DispatcherThread:
    case DispatcherProcess:
        // Notification events remain signaled until reset. Terminated thread
        // and process objects remain signaled permanently.
        return MT_SUCCESS;

    case DispatcherSynchronizationEvent:
        // A synchronization event consumes its one stored signal.
        Header->SignalState = 0;
        return MT_SUCCESS;

    case DispatcherSemaphore:
        // Consume one available semaphore permit.
        assert(Header->SignalState > 0);
        Header->SignalState--;
        return MT_SUCCESS;

    case DispatcherMutex: {
        // Mutex is available to be taken (or we are the owner thread and allowed to recursively acquire).
        PMUTEX Mutex = (PMUTEX)Header;

        assert(Header->SignalState > 0 || Mutex->OwnerThread == Thread);

        if (Header->SignalState == INT32_MIN) {
            return MT_MUTEX_LIMIT_EXCEEDED; // Would terminate
        }

        // SignalState alone tracks recursion: 1 is free, 0 is the first
        // acquisition, and each additional recursive acquisition is negative.
        Header->SignalState--;

        if (Header->SignalState == 0) {
            Mutex->OwnerThread = Thread;

            // Link the mutex to the thread's owned mutexes list.
            MsAcquireSpinlockAtDpcLevel(&Thread->InternalThread.OwnedMutexesListLock);
            InsertTailList(&Thread->InternalThread.OwnedMutexListHead, &Mutex->OwnerListEntry);
            MsReleaseSpinlockFromDpcLevel(&Thread->InternalThread.OwnedMutexesListLock);

            if (Mutex->Abandoned) {
                Mutex->Abandoned = false;
                return MT_MUTEX_ABANDONED;
            }
        }

        return MT_SUCCESS;
    }

    default:
        MeBugCheckEx(
            WRONG_DISPATCHER_HEADER,
            Header,
            Thread,
            NULL,
            NULL
        );
    }
}

MTSTATUS
MsWaitForSingleObject(
    IN void* Object,
    IN PRIVILEGE_MODE WaitMode, // Used later for paging out stacks, TODO
    IN bool Alertable,
    IN uint64_t TimeoutMs
)

/*++

    Routine Description:

        Waits until the specified dispatcher object becomes satisfied or the
        timeout expires. Alertable waits are not implemented yet.

    Arguments:

        Object -

            Pointer to the dispatcher object to wait on.

        WaitMode -

            Reserved for future kernel-stack paging policy. Currently ignored.

        Alertable -

            Reserved for future alert/APC interruption. Currently ignored.

        TimeoutMs -

            Specifies the maximum amount of time, in milliseconds, that
            the thread may wait for the object to become signaled.

    Return Value:

        Returns MT_SUCCESS when the object satisfies the wait, MT_TIMEOUT when
        the deadline wins, MT_MUTEX_ABANDONED when ownership is inherited from
        a dead mutex owner, or another wait/teardown failure status.

--*/

{
    assert(Object); // add more assertions
    UNREFERENCED_PARAMETER(WaitMode); UNREFERENCED_PARAMETER(Alertable);

    PDISPATCHER_HEADER Header = (PDISPATCHER_HEADER)Object;
    PETHREAD Thread = PsGetCurrentThread();

    IRQL dispatcherIrql;
    MsAcquireSpinlock(&Header->Lock, &dispatcherIrql);

    // If we can satisfy a dispatcher object without immediately waiting, then it is the favorable fast path.
    if (MspCanSatisfyDispatcherObject(Header, Thread)) {
        MTSTATUS Status = MspSatisfyDispatcherObject(Header, Thread);
        MsReleaseSpinlock(&Header->Lock, dispatcherIrql);
        return Status;
    }

    if (TimeoutMs == 0) {
        // Thread just wants an answer for this object type, and if it can be immediately claimed.
        MsReleaseSpinlock(&Header->Lock, dispatcherIrql);
        return MT_TIMEOUT;
    }

    // The object cannot satisfy the wait now, so register and park the thread.
    Thread->InternalThread.WaitStatus = MT_PENDING;
    Thread->InternalThread.ThreadState = THREAD_BLOCKING;
    Thread->InternalThread.WaitBlock.Object = Header;
    Thread->InternalThread.WaitBlock.WaitReason = WaitReasonDispatcherObject;

    // Register the embedded wait block in this object's protected wait list.
    InsertTailList(&Header->WaitListHead, &Thread->InternalThread.WaitBlock.ObjectListEntry);

    // Now put inside timer queue IF a valid timeout is provided
    if (TimeoutMs != MT_INFINITE) {
        // Convert the relative millisecond timeout to timer ticks.
        uint64_t Ticks = TimeoutMs / TICK_MS;
        
        // Round up so any nonzero sub-tick timeout waits for at least one tick.
        if (TimeoutMs    % TICK_MS) Ticks++;

        // Calculate absolute time in the system to wake the thread up
        uint64_t Now = InterlockedLoadAcquire(&MeSystemTickCount);

        // Saturate at UINT64_MAX instead of wrapping a very large timeout into the past.
        uint64_t WakeupTime = WILL_ADD_OVERFLOW(Now, Ticks) ? UINT64_MAX : Now + Ticks;

        MsInsertTimerQueue(&Thread->InternalThread, WakeupTime);
    }

    // Publish the completed registration by releasing the dispatcher lock.
    MsReleaseSpinlock(&Header->Lock, dispatcherIrql);

    // Yield Execution.
    MtYield();

    // The winning wake source published the final status before making this
    // thread runnable. This may be success, timeout, abandonment, or teardown.
    MTSTATUS finalStatus = Thread->InternalThread.WaitStatus;

    // The winning wake path removes both queue registrations before completing
    // the wait; MsCompleteThreadWait asserts that ownership rule.

    // Clear metadata before this embedded wait block is reused.
    MeClearWaitBlock(&Thread->InternalThread);

    return finalStatus;
}
