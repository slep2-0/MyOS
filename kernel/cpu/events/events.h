/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:		 Event Headers and Prototypes (refer to KeSetEvent in windows)
 */

#ifndef X86_EVENT_H
#define X86_EVENT_H

#include "../cpu.h"

/// <summary>
/// Signals an EVENT struct to wake up threads. (Acquires Spinlock)
/// </summary>
/// <param name="Event">Pointer to EVENT variable.</param>
/// <returns>MTSTATUS Status Code.</returns>
static MTSTATUS MtSetEvent(EVENT* event) {
    if (!event) return MT_INVALID_ADDRESS;
    if (!event->lock) return MT_INVALID_LOCK;

    uint64_t flags;
    MtAcquireSpinlock(event->lock, &flags);

    if (event->type == SynchronizationEvent) {
        // Wake exactly one waiter (auto-reset)
        Thread* waiter = MtDequeueThread(event->waitingQueue); // safe under event->lock
        if (waiter) {
            event->signaled = false; // consumed by waking one waiter
            MtReleaseSpinlock(event->lock, flags);

            waiter->threadState = READY;
            MtEnqueueThreadWithLock(&cpu.readyQueue, waiter);
            return MT_SUCCESS;
        }
        else {
            // No waiter -> mark event signaled so next waiter won't block
            event->signaled = true;
            MtReleaseSpinlock(event->lock, flags);
            return MT_SUCCESS;
        }
    }

    // NotificationEvent: drain waiters into local list while holding event lock
    Thread* head = NULL;
    Thread* tail = NULL;
    Thread* t;
    while ((t = MtDequeueThread(event->waitingQueue)) != NULL) {
        t->nextThread = NULL;
        if (tail) tail->nextThread = t;
        else head = t;
        tail = t;
    }

    // Notification persists until reset
    event->signaled = true;
    MtReleaseSpinlock(event->lock, flags);

    // Enqueue drained threads to scheduler (after releasing event lock)
    t = head;
    while (t) {
        Thread* nxt = t->nextThread;
        t->threadState = READY;
        MtEnqueueThreadWithLock(&cpu.readyQueue, t);
        t = nxt;
    }

    return MT_SUCCESS;
}

static MTSTATUS MtWaitForEvent(EVENT* event) {
    if (!event) return MT_INVALID_ADDRESS;
    if (!event->lock) return MT_INVALID_LOCK;

    uint64_t flags;
    Thread* curr = MtGetCurrentThread();

    // Acquire event lock to check signaled state atomically with enqueue.
    MtAcquireSpinlock(event->lock, &flags);

    // If already signaled, consume or accept depending on type:
    if (event->signaled) {
        if (event->type == SynchronizationEvent) {
            // consume the single-signaled state
            event->signaled = false;
        }
        // For NotificationEvent, leave event->signaled = true (notification persists)
        MtReleaseSpinlock(event->lock, flags);
        return MT_SUCCESS;
    }

    // Not signaled -> enqueue this thread into the event waiting queue (under event lock)
    MtEnqueueThread(event->waitingQueue, curr);
    // Keep event lock held only for enqueue; after this we release and block.
    MtReleaseSpinlock(event->lock, flags);

    // Block the thread. When MtSetEvent wakes it, it will be placed on ready queue.
    curr->threadState = BLOCKED;
    MtSleepCurrentThread();

    // When we resume here, the waker has already moved us to the ready queue.
    return MT_SUCCESS;
}



#endif
