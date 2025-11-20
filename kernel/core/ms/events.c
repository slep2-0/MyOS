/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:		 Events Implementation (see KeSetEvent and KMUTANT in MSDN)
 */

#include "../../includes/me.h"
#include "../../includes/ps.h"
#include "../../includes/mg.h"
#include "../../assert.h"

MTSTATUS 
MsSetEvent (
    IN  PEVENT event
) 

/*++

    Routine description : Sets an event to wake threads waiting on it.

    Arguments:
    
        Pointer to EVENT object.

    Return Values:

        Varuious MTSTATUS Codes.

--*/

{
    if (!event) return MT_INVALID_ADDRESS;

    IRQL flags;
    MsAcquireSpinlock(&event->lock, &flags);

    if (event->type == SynchronizationEvent) {
        // Wake exactly one waiter (auto-reset)
        PETHREAD waiter = MeDequeueThread(&event->waitingQueue); // safe under event->lock
        if (waiter) {
            event->signaled = false; // consumed by waking one waiter
            MsReleaseSpinlock(&event->lock, flags);

            waiter->InternalThread.ThreadState = THREAD_READY;
            MeEnqueueThreadWithLock(&MeGetCurrentProcessor()->readyQueue, waiter);
            return MT_SUCCESS;
        }
        else {
            // No waiter -> mark event signaled so next waiter won't block
            event->signaled = true;
            MsReleaseSpinlock(&event->lock, flags);
            return MT_SUCCESS;
        }
    }

    // NotificationEvent: drain waiters into local list while holding event lock
    PETHREAD head = NULL;
    PETHREAD tail = NULL;
    PETHREAD t;
    while ((t = MeDequeueThread(&event->waitingQueue)) != NULL) {
        t->nextThread = NULL;
        if (tail) tail->nextThread = t;
        else head = t;
        tail = t;
    }

    // Notification persists until reset
    event->signaled = true;
    MsReleaseSpinlock(&event->lock, flags);

    // Enqueue drained threads to scheduler (after releasing event lock)
    t = head;
    while (t) {
        PETHREAD nxt = t->nextThread;
        t->InternalThread.ThreadState = THREAD_READY;
        MeEnqueueThreadWithLock(&MeGetCurrentProcessor()->readyQueue, t);
        t = nxt;
    }

    return MT_SUCCESS;
}

MTSTATUS MsWaitForEvent (
    IN  PEVENT event
) 

/*++

    Routine description : Sleeps the current thread to wait on the specified event.

    Arguments:

        Pointer to EVENT Object.

    Return Values:

        MT_SUCCESS on wake, other MTSTATUS codes for failure.

    Notes:
        
        This function MUST NOT be called on IRQL higher or equal to DISPATCH_LEVEL, as this function is blocking or uses pageable memory.

--*/

{
    if (!event) return MT_INVALID_ADDRESS;
    assert((MeGetCurrentIrql() < DISPATCH_LEVEL), "Blocking function called with DISPATCH_LEVEL IRQL or Higher.");
    IRQL flags;
    PETHREAD curr = PsGetCurrentThread();

    // Acquire event lock to check signaled state atomically with enqueue.
    MsAcquireSpinlock(&event->lock, &flags);

    // If already signaled, consume or accept depending on type:
    if (event->signaled) {
        if (event->type == SynchronizationEvent) {
            // consume the single-signaled state
            event->signaled = false;
        }
        // For NotificationEvent, leave event->signaled = true (notification persists)
        MsReleaseSpinlock(&event->lock, flags);
        return MT_SUCCESS;
    }

    // Block the thread. When MtSetEvent wakes it, it will be placed on ready queue.
    curr->InternalThread.ThreadState = THREAD_BLOCKED;
    curr->CurrentEvent = event;
    // Not signaled -> enqueue this thread into the event waiting queue (under event lock)
    MeEnqueueThread(&event->waitingQueue, curr);
    // Keep event lock held only for enqueue; after this we release and block.
    MsReleaseSpinlock(&event->lock, flags);
#ifdef DEBUG
    gop_printf(COLOR_PURPLE, "Sleeping current thread: %p\n", PsGetCurrentThread());
#endif
    MsSleepCurrentThread(&curr->InternalThread.TrapRegisters);

    // When we resume here, the waker has already moved us to the ready queue, and we are now an active thread on the CPU.
    return MT_SUCCESS;
}

