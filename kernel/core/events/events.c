/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:		 Events Implementation (see KeSetEvent and KMUTANT in MSDN)
 */

#include "events.h"

MTSTATUS MtSetEvent(EVENT* event) {
    if (!event) return MT_INVALID_ADDRESS;

    IRQL flags;
    MtAcquireSpinlock(&event->lock, &flags);

    if (event->type == SynchronizationEvent) {
        // Wake exactly one waiter (auto-reset)
        Thread* waiter = MtDequeueThread(&event->waitingQueue); // safe under event->lock
        if (waiter) {
            event->signaled = false; // consumed by waking one waiter
            MtReleaseSpinlock(&event->lock, flags);

            waiter->threadState = READY;
            MtEnqueueThreadWithLock(&thisCPU()->readyQueue, waiter);
            return MT_SUCCESS;
        }
        else {
            // No waiter -> mark event signaled so next waiter won't block
            event->signaled = true;
            MtReleaseSpinlock(&event->lock, flags);
            return MT_SUCCESS;
        }
    }

    // NotificationEvent: drain waiters into local list while holding event lock
    Thread* head = NULL;
    Thread* tail = NULL;
    Thread* t;
    while ((t = MtDequeueThread(&event->waitingQueue)) != NULL) {
        t->nextThread = NULL;
        if (tail) tail->nextThread = t;
        else head = t;
        tail = t;
    }

    // Notification persists until reset
    event->signaled = true;
    MtReleaseSpinlock(&event->lock, flags);

    // Enqueue drained threads to scheduler (after releasing event lock)
    t = head;
    while (t) {
        Thread* nxt = t->nextThread;
        t->threadState = READY;
        MtEnqueueThreadWithLock(&thisCPU()->readyQueue, t);
        t = nxt;
    }

    return MT_SUCCESS;
}

MTSTATUS MtWaitForEvent(EVENT* event) {
    if (!event) return MT_INVALID_ADDRESS;

    IRQL flags;
    Thread* curr = MtGetCurrentThread();

    // Acquire event lock to check signaled state atomically with enqueue.
    MtAcquireSpinlock(&event->lock, &flags);

    // If already signaled, consume or accept depending on type:
    if (event->signaled) {
        if (event->type == SynchronizationEvent) {
            // consume the single-signaled state
            event->signaled = false;
        }
        // For NotificationEvent, leave event->signaled = true (notification persists)
        MtReleaseSpinlock(&event->lock, flags);
        return MT_SUCCESS;
    }

    // Block the thread. When MtSetEvent wakes it, it will be placed on ready queue.
    curr->threadState = BLOCKED;
    curr->CurrentEvent = event;
    // Not signaled -> enqueue this thread into the event waiting queue (under event lock)
    MtEnqueueThread(&event->waitingQueue, curr);
    // Keep event lock held only for enqueue; after this we release and block.
    MtReleaseSpinlock(&event->lock, flags);
#ifdef DEBUG
    gop_printf(COLOR_PURPLE, "Sleeping current thread: %p\n", MtGetCurrentThread());
#endif
    MtSleepCurrentThread(&curr->registers);

    // When we resume here, the waker has already moved us to the ready queue.
    return MT_SUCCESS;
}

