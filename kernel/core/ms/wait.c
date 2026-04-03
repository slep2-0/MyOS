#include "../../includes/ms.h"
#include "../../includes/me.h"
#include "../../includes/ps.h"
#include "../../includes/mg.h"

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
    gop_printf(COLOR_CYAN, "In TimerExpirationDPC.\n");

    IRQL oldTimerIrql;
    MeRaiseIrql(CLOCK_LEVEL, &oldTimerIrql);

    // Manually spin
    while (__sync_lock_test_and_set(&MsTimerQueueLock.locked, 1)) {
        __asm__ volatile("pause" ::: "memory");
    }

    while (!IsListEmpty(&MsTimerQueue)) {
        PITHREAD Thread = GetHeadOfTimerQueue();

        if (!Thread || Thread->WaitBlock.WakeupTime > MeSystemTickCount) {
            break;
        }

        RemoveEntryList(&Thread->WaitBlock.WaitBlockList);
        Thread->WaitBlock.WaitBlockList.Flink = NULL;
        Thread->WaitBlock.WaitBlockList.Blink = NULL;

        if (__sync_val_compare_and_swap(&Thread->WaitStatus, MT_PENDING, MT_TIMEOUT) == MT_PENDING) {
            Thread->ThreadState = THREAD_READY;


            // Release the CLOCK_LEVEL lock temporarily
            __sync_lock_release(&MsTimerQueueLock.locked);
            MeLowerIrql(oldTimerIrql);

            gop_printf(COLOR_CYAN, "TimerExpirationDPC: Enqueuing thread %p into processor\n", Thread);
            // Safe to acquire readyQueue at DISPATCH_LEVEL
            Queue* readyQueue = &MeGetCurrentProcessor()->readyQueue;
            MsAcquireSpinlockAtDpcLevel(&readyQueue->lock);
            MeEnqueueThread(readyQueue, PsGetEThreadFromIThread(Thread));
            MeGetCurrentProcessor()->schedulePending = true;
            MsReleaseSpinlockFromDpcLevel(&readyQueue->lock);

            // Re-acquire the timer queue lock at CLOCK_LEVEL
            MeRaiseIrql(CLOCK_LEVEL, &oldTimerIrql);
            while (__sync_lock_test_and_set(&MsTimerQueueLock.locked, 1)) {
                __asm__ volatile("pause" ::: "memory");
            }
            gop_printf(COLOR_CYAN, "TimerExpirationDPC: Claimed thread and inserted into current processor queue\n");
        }
    }

    gop_printf(COLOR_CYAN, "TimerExpirationDPC: Leaving.\n");
    __sync_lock_release(&MsTimerQueueLock.locked);
    MeLowerIrql(oldTimerIrql);
}