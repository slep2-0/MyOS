/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:		 Events Implementation (see KeSetEvent and KMUTANT in MSDN)
 */

#include "../../includes/me.h"
#include "../../includes/ps.h"
#include "../../includes/mg.h"
#include "../../includes/ms.h"
#include "../../assert.h"

MTSTATUS 
MsSetEvent (
    IN  PEVENT event
) 

/*++

    Routine description : 
    
        Sets an event to wake threads waiting on it.

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
        PETHREAD waiter;
        while ((waiter = MeDequeueThread(&event->waitingQueue)) != NULL) {

            // Try to claim the thread for a Success wake
            if (MsClaimThreadWait(&waiter->InternalThread, MT_SUCCESS)) {
                event->signaled = false;
                MsReleaseSpinlock(&event->lock, flags);

                MsCompleteThreadWait(&waiter->InternalThread);
                return MT_SUCCESS;
            }
            // If failed, the timer claimed it. Loop to find the next valid waiter.
        }

        event->signaled = true;
        MsReleaseSpinlock(&event->lock, flags);
        return MT_SUCCESS;
    }

    // Notif
    PETHREAD t;
    while ((t = MeDequeueThread(&event->waitingQueue)) != NULL) {
        // MeDequeueThread already isolates the SchedulerListEntry, so no need to nullify anything else.

        // Only wake threads we successfully claim
        if (MsClaimThreadWait(&t->InternalThread, MT_SUCCESS)) {
            MsCompleteThreadWait(&t->InternalThread);
        }
    }

    event->signaled = true;
    MsReleaseSpinlock(&event->lock, flags);
    return MT_SUCCESS;
}

MTSTATUS 
MsWaitForEvent (
    IN  PEVENT event,
    IN uint64_t Milliseconds
) 

/*++

    Routine description : 
    
        Sleeps the current thread to wait on the specified event.

    Arguments:

        Pointer to EVENT Object.
        Amount of milliseconds to wait if event doesnt signal us.

    Return Values:

        MT_SUCCESS on wake, other MTSTATUS codes for failure.

    Notes:
        
        This function MUST NOT be called on IRQL higher or equal to DISPATCH_LEVEL, as this function is blocking or uses pageable memory.

--*/

{
    if (!event) return MT_INVALID_ADDRESS;
    assert((MeGetCurrentIrql() < DISPATCH_LEVEL));

    IRQL flags;
    PETHREAD curr = PsGetCurrentThread();

    MsAcquireSpinlock(&event->lock, &flags);

    if (event->signaled) {
        if (event->type == SynchronizationEvent) event->signaled = false;
        MsReleaseSpinlock(&event->lock, flags);
        return MT_SUCCESS;
    }

    // Zero timeout check
    if (Milliseconds == 0) {
        MsReleaseSpinlock(&event->lock, flags);
        return MT_TIMEOUT;
    }

    // Setup atomic claim and block state
    curr->InternalThread.WaitStatus = MT_PENDING;
    curr->CurrentEvent = event;
    curr->InternalThread.ThreadState = THREAD_BLOCKING;

    // Enqueue into the Event waiting queue
    MeEnqueueThread(&event->waitingQueue, curr);

    // Enqueue into Timer Queue if a valid timeout is provided
    if (Milliseconds != INFINITE) {
        uint64_t Ticks = Milliseconds / TICK_MS;
        if (Milliseconds % TICK_MS) Ticks++;
        uint64_t Now = __atomic_load_n(&MeSystemTickCount, __ATOMIC_ACQUIRE);
        uint64_t WakeupTime = Ticks > UINT64_MAX - Now
            ? UINT64_MAX
            : Now + Ticks;
        MsInsertTimerQueue(&curr->InternalThread, WakeupTime, Sleeping);
    }

    MsReleaseSpinlock(&event->lock, flags);

#ifdef DEBUG
    gop_printf(COLOR_PURPLE, "Sleeping current thread: %p (owner %s)\n", PsGetCurrentThread(), PsGetCurrentProcess()->ImageName);
#endif

    // Yield Execution
    MsYieldExecution(&curr->InternalThread.TrapRegisters);

    // At this point, WaitStatus is either MT_SUCCESS or MT_TIMEOUT
    MTSTATUS finalStatus = curr->InternalThread.WaitStatus;

    // Unlink from queues to prevent memory corruption
    if (finalStatus == MT_TIMEOUT) {
        MsAcquireSpinlock(&event->lock, &flags);
        MeRemoveThreadFromQueue(&event->waitingQueue, curr);
        MsReleaseSpinlock(&event->lock, flags);
    }
    else if (finalStatus == MT_SUCCESS && Milliseconds != INFINITE) {
        // Event woke us, so we might still be in the Timer queue.
        MsRemoveTimerQueue(&curr->InternalThread);
    }

    curr->CurrentEvent = NULL;
    return finalStatus;
}
