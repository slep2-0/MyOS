#include "../../includes/ms.h"
#include "../../includes/me.h"
#include "../../includes/ps.h"
#include "../../includes/mg.h"

SPINLOCK MsTimerQueueLock;
DOUBLY_LINKED_LIST MsTimerQueue;

static
bool
MspIsTimerQueued(
    IN PITHREAD Thread
)
{
    PDOUBLY_LINKED_LIST Entry = &Thread->WaitBlock.WaitBlockList;

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
    PDOUBLY_LINKED_LIST Entry = &Thread->WaitBlock.WaitBlockList;

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
        PITHREAD Block = CONTAINING_RECORD(Current, ITHREAD, WaitBlock.WaitBlockList);

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
    IN uint64_t WakeupTime,
    IN int WaitReason
)
{
    IRQL OldIrql;

    Thread->WaitBlock.WakeupTime = WakeupTime;
    Thread->WaitBlock.WaitReason = (WAIT_REASON)WaitReason;

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
        RemoveEntryList(&Thread->WaitBlock.WaitBlockList);
        InitializeListHead(&Thread->WaitBlock.WaitBlockList);
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
    // THREAD_BLOCKING still owns a live kernel stack on its current CPU. The
    // scheduler observes the completed WaitStatus and makes it runnable as
    // part of the switch-away transaction.
    if (__sync_val_compare_and_swap(
        &Thread->ThreadState,
        THREAD_BLOCKED,
        THREAD_READY
    ) != THREAD_BLOCKED) {
        return;
    }

    PPROCESSOR TargetProcessor = __atomic_load_n(
        &Thread->ActiveProcessor,
        __ATOMIC_ACQUIRE
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
    if (!IsListEmpty(&MsTimerQueue)) { Head = CONTAINING_RECORD(MsTimerQueue.Flink, ITHREAD, WaitBlock.WaitBlockList); }
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

        RemoveEntryList(&Thread->WaitBlock.WaitBlockList);
        InitializeListHead(&Thread->WaitBlock.WaitBlockList);

        if (MsClaimThreadWait(Thread, MT_TIMEOUT)) {
            PETHREAD EThread = PsGetEThreadFromIThread(Thread);
            PEVENT Event = EThread->CurrentEvent;

            // Release the CLOCK_LEVEL lock temporarily
            MsReleaseSpinlockFromDpcLevel(&MsTimerQueueLock);
            MeLowerIrql(oldTimerIrql);

            if (Event) {
                IRQL eventIrql;
                MsAcquireSpinlock(&Event->lock, &eventIrql);
                MeRemoveThreadFromQueue(&Event->waitingQueue, EThread);
                EThread->CurrentEvent = NULL;
                MsReleaseSpinlock(&Event->lock, eventIrql);
            }

            MsCompleteThreadWait(Thread);

            // Re-acquire the timer queue lock at CLOCK_LEVEL
            MeRaiseIrql(CLOCK_LEVEL, &oldTimerIrql);
            MsAcquireSpinlockAtDpcLevel(&MsTimerQueueLock);
        }
    }

    MsReleaseSpinlockFromDpcLevel(&MsTimerQueueLock);
    MeLowerIrql(oldTimerIrql);
}
