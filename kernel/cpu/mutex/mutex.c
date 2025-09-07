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

    IRQL oldirql;
    MtAcquireSpinlock(&mut->lock, &oldirql);

    bool isValid = MtIsAddressValid((void*)mut);
    assert((isValid) == 1, "MUTEX Pointer given to function isn't paged in.");
    if (!isValid) {
        MtReleaseSpinlock(&mut->lock, oldirql);
        return MT_INVALID_ADDRESS;
    }

    assert((mut->ownerTid) == 0, "Mutex must not be owned already in initialization.");
    if (mut->ownerTid) {
        MtReleaseSpinlock(&mut->lock, oldirql);
        return MT_MUTEX_ALREADY_OWNED;
    }

    mut->ownerTid = 0;
    mut->locked = false;

    // Initialize the event state (event->lock is separate and must be preallocated)
    // Initialize waiting queue under event lock for safety
    {
        IRQL eflags;
        MtAcquireSpinlock(&mut->SynchEvent.lock, &eflags);
        mut->SynchEvent.type = SynchronizationEvent;
        mut->SynchEvent.signaled = false;
        mut->SynchEvent.waitingQueue.head = mut->SynchEvent.waitingQueue.tail = NULL;
        MtReleaseSpinlock(&mut->SynchEvent.lock, eflags);
    }

    MtReleaseSpinlock(&mut->lock, oldirql);
    return MT_SUCCESS;
}

MTSTATUS MtAcquireMutexObject(MUTEX* mut) {
    if (!mut) return MT_INVALID_ADDRESS;
    gop_printf(COLOR_PURPLE, "MtAcquireMutex hit - thread: %p | mut: %p\n", MtGetCurrentThread(), mut);
    IRQL mflags;
    MtAcquireSpinlock(&mut->lock, &mflags);
    bool isValid = MtIsAddressValid((void*)mut);
    assert((isValid) == true, "MUTEX Pointer given to function isn't paged in.");
    if (!isValid) {
        MtReleaseSpinlock(&mut->lock, mflags);
        return MT_INVALID_ADDRESS;
    }
    Thread* currThread = MtGetCurrentThread();
    if (!mut->locked) {
        // Acquire the mutex while holding mut->lock
        mut->locked = true;
        mut->ownerTid = currThread->TID;
        MtReleaseSpinlock(&mut->lock, mflags);
        gop_printf(COLOR_RED, "[MUTEX-DEBUG] Mutex successfully acquired by: %p. MUT: %p\n", currThread, mut);
        return MT_SUCCESS;
    }
    gop_printf(COLOR_PURPLE, "e");
    gop_printf(COLOR_RED, "[MUTEX-DEBUG] Mutex was attempted to be acquired when it is already locked. MUT: %p\n", mut);
    // Mutex is owned -> wait for event.
    MtReleaseSpinlock(&mut->lock, mflags);
    MtWaitForEvent(&mut->SynchEvent);
    gop_printf(COLOR_GREEN, "[MUTEX-DEBUG] Mutex re-acquired by %p | MUT: %p\n", currThread, mut);
    // When woken, the releaser has transferred ownership while holding locks.
    return MT_SUCCESS;
}

MTSTATUS MtReleaseMutexObject(MUTEX* mut) {
    if (!mut) return MT_INVALID_ADDRESS;

    // FOLLOW LOCK ORDER: acquire mut->lock then event->lock
    IRQL mflags;
    MtAcquireSpinlock(&mut->lock, &mflags);

    assert((mut->ownerTid) != 0, "Attempted release of mutex when it has no owner.");
    if (!mut->ownerTid) {
        MtReleaseSpinlock(&mut->lock, mflags);
        return MT_MUTEX_NOT_OWNED;
    }

    IRQL eflags;
    MtAcquireSpinlock(&mut->SynchEvent.lock, &eflags);

    // Dequeue a waiter while holding event->lock (and still holding mut->lock)
    Thread* next = MtDequeueThread(&mut->SynchEvent.waitingQueue);

    if (!next) {
        // No waiter: release mutex
        mut->locked = false;
        mut->ownerTid = 0;
        MtReleaseSpinlock(&mut->SynchEvent.lock, eflags);
        MtReleaseSpinlock(&mut->lock, mflags);
        return MT_SUCCESS;
    }

    // There is a waiter: transfer ownership atomically while holding both locks
    mut->ownerTid = next->TID;
    mut->locked = true; // stays locked but now owned by 'next'

    // We've removed 'next' from the waiting queue already
    MtReleaseSpinlock(&mut->SynchEvent.lock, eflags);
    MtReleaseSpinlock(&mut->lock, mflags);

    // Wake the selected thread by setting an event.
    MtSetEvent(&mut->SynchEvent);

    return MT_SUCCESS;
}