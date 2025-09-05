/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     GPLv3
 * PURPOSE:     Mutex Implementation.
 */

#include "mutex.h"
#include "../../assert.h"
#include "../events/events.h"

MTSTATUS MtInitializeMutexObject(MUTEX* mut) {
    if (!mut) return MT_INVALID_ADDRESS;
    if (!mut->lock) return MT_INVALID_LOCK;
    if (!mut->SynchEvent) return MT_INVALID_ADDRESS;
    if (!mut->SynchEvent->lock) return MT_INVALID_LOCK; // event must have its own lock allocated

    uint64_t flags;
    MtAcquireSpinlock(mut->lock, &flags);

    bool isValid = MtIsAddressValid((void*)mut);
    assert((isValid) == 1, "MUTEX Pointer given to function isn't paged in.");
    if (!isValid) {
        MtReleaseSpinlock(mut->lock, flags);
        return MT_INVALID_ADDRESS;
    }

    assert((mut->ownerTid) == 0, "Mutex must not be owned already in initialization.");
    if (mut->ownerTid) {
        MtReleaseSpinlock(mut->lock, flags);
        return MT_MUTEX_ALREADY_OWNED;
    }

    mut->ownerTid = 0;
    mut->locked = false;

    // Initialize the event state (event->lock is separate and must be preallocated)
    // Initialize waiting queue under event lock for safety
    {
        uint64_t eflags;
        MtAcquireSpinlock(mut->SynchEvent->lock, &eflags);
        mut->SynchEvent->type = SynchronizationEvent;
        mut->SynchEvent->signaled = false;
        mut->SynchEvent->waitingQueue->head = mut->SynchEvent->waitingQueue->tail = NULL;
        MtReleaseSpinlock(mut->SynchEvent->lock, eflags);
    }

    MtReleaseSpinlock(mut->lock, flags);
    return MT_SUCCESS;
}

MTSTATUS MtAcquireMutexObject(MUTEX* mut) {
    if (!mut) return MT_INVALID_ADDRESS;
    if (!mut->lock) return MT_INVALID_LOCK;
    if (!mut->SynchEvent) return MT_INVALID_ADDRESS;
    if (!mut->SynchEvent->lock) return MT_INVALID_LOCK;

    uint64_t mflags;
    MtAcquireSpinlock(mut->lock, &mflags);

    bool isValid = MtIsAddressValid((void*)mut);
    assert((isValid) == 1, "MUTEX Pointer given to function isn't paged in.");
    if (!isValid) {
        MtReleaseSpinlock(mut->lock, mflags);
        return MT_INVALID_ADDRESS;
    }

    Thread* currThread = MtGetCurrentThread();

    if (!mut->locked) {
        // Acquire the mutex while holding mut->lock
        mut->locked = true;
        mut->ownerTid = currThread->TID;
        MtReleaseSpinlock(mut->lock, mflags);
        return MT_SUCCESS;
    }

    // Mutex is owned -> wait for event.
    MtWaitForEvent(mut->SynchEvent);

    // When woken, the releaser has transferred ownership while holding locks.
    return MT_SUCCESS;
}

MTSTATUS MtReleaseMutexObject(MUTEX* mut) {
    if (!mut) return MT_INVALID_ADDRESS;
    if (!mut->lock) return MT_INVALID_LOCK;
    if (!mut->SynchEvent) return MT_INVALID_ADDRESS;
    if (!mut->SynchEvent->lock) return MT_INVALID_LOCK;

    // FOLLOW LOCK ORDER: acquire mut->lock then event->lock
    uint64_t mflags;
    MtAcquireSpinlock(mut->lock, &mflags);

    assert((mut->ownerTid) != 0, "Attempted release of mutex when it has no owner.");
    if (!mut->ownerTid) {
        MtReleaseSpinlock(mut->lock, mflags);
        return MT_MUTEX_NOT_OWNED;
    }

    uint64_t eflags;
    MtAcquireSpinlock(mut->SynchEvent->lock, &eflags);

    // Dequeue a waiter while holding event->lock (and still holding mut->lock)
    Thread* next = MtDequeueThread(mut->SynchEvent->waitingQueue);

    if (!next) {
        // No waiter: release mutex
        mut->locked = false;
        mut->ownerTid = 0;
        MtReleaseSpinlock(mut->SynchEvent->lock, eflags);
        MtReleaseSpinlock(mut->lock, mflags);
        return MT_SUCCESS;
    }

    // There is a waiter: transfer ownership atomically while holding both locks
    mut->ownerTid = next->TID;
    mut->locked = true; // stays locked but now owned by 'next'

    // We've removed 'next' from the waiting queue already
    MtReleaseSpinlock(mut->SynchEvent->lock, eflags);
    MtReleaseSpinlock(mut->lock, mflags);

    // Wake the selected thread by setting an event.
    MtSetEvent(mut->SynchEvent);

    return MT_SUCCESS;
}