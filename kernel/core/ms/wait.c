#include "../../includes/ms.h"
#include "../../includes/me.h"
#include "../../includes/ps.h"

SPINLOCK MsTimerQueueLock;
DOUBLY_LINKED_LIST MsTimerQueue;

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

    MsAcquireSpinlockAtDpcLevel(&MsTimerQueueLock);

    while (!IsListEmpty(&MsTimerQueue)) {
        PITHREAD Thread = GetHeadOfTimerQueue();

        if (!Thread || Thread->WaitBlock.WakeupTime > MeSystemTickCount) {
            break;
        }

        // Pop it off the timer queue
        RemoveEntryList(&Thread->WaitBlock.WaitBlockList);
        Thread->WaitBlock.WaitBlockList.Flink = NULL; // Mark as unlinked
        Thread->WaitBlock.WaitBlockList.Blink = NULL;

        // Try to atomically claim this thread for a Timeout wake
        if (__sync_val_compare_and_swap(&Thread->WaitStatus, MT_PENDING, MT_TIMEOUT) == MT_PENDING) {
            // We claimed it! Wake it up.
            Thread->ThreadState = THREAD_READY;
            MeEnqueueThreadWithLock(&MeGetCurrentProcessor()->readyQueue, PsGetEThreadFromIThread(Thread));
        }
        // If it fails, the event already woke it up. We just cleaned it out of the timer queue.
    }

    MsReleaseSpinlockFromDpcLevel(&MsTimerQueueLock);
}