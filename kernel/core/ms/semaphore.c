/*++

Module Name:

    semaphore.c

Purpose:

    This translation unit contains the implementation of the semaphore synchronization object.

Author:

    slep (Matanel) 2026.

Revision History:

--*/

#include "../../includes/ms.h"
#include "../../assert.h"

void
MsInitializeSemaphore(
    IN PSEMAPHORE Semaphore,
    IN int32_t Count,
    IN int32_t Limit
)

/*++

    Routine description:

        Initializes a SEMAPHORE Synchronization Object with the specified count and upper limit that count can attain.

    Arguments:

        [IN]    Semaphore - Caller-supplied resident storage for the semaphore.
        [IN]    Count - Initial permit count from zero through Limit. A nonzero
                       count makes the semaphore signaled.
        [IN]    Limit - Positive maximum number of permits that may be stored.


    Return Values:

        None.

--*/

{
    assert(Count > -1 && Limit > 0 && Count <= Limit);
    kmemset(Semaphore, 0, sizeof(SEMAPHORE));

    // Initialize the dispatcher header for the semaphore.
    MsInitializeDispatcherHeader(&Semaphore->Header, Count, DispatcherSemaphore);

    // Set the limit
    Semaphore->Limit = Limit;
}

int32_t
MsReleaseSemaphore(
    IN PSEMAPHORE Semaphore,
    IN int32_t Adjustment
)

/*++

    Routine description:

        Releases a Semaphore object.

    Arguments:

        [IN]    Semaphore - Semaphore whose permit count is increased.
        [IN]    Adjustment - Positive number of permits to release.


    Return Values:

        Previous count of the semaphore

--*/

{
    assert(Adjustment > 0);

    // Lock the dispatcher object.
    IRQL dispatcherIrql;
    MsAcquireSpinlock(&Semaphore->Header.Lock, &dispatcherIrql);

    // Perform some important runtime assesments first.
    // The caller must not provide an adjustment that would go over the limit.
    // Also make it immune to signed overflow.
    if (Adjustment > Semaphore->Limit - Semaphore->Header.SignalState) {
        // You cannot add the count higher than its limit.
        // Normally this would raise an exception, but there is no kernel exception handler yet, so this is a fatal error.
        MsReleaseSpinlock(&Semaphore->Header.Lock, dispatcherIrql);
        MeBugCheckEx(
            SEMAPHORE_LIMIT_REACHED,
            Semaphore,
            (void*)(uintptr_t)Adjustment,
            MeGetCurrentThread(),
            NULL
        );
    }

    // Save the previous permit count for the return value.
    int32_t PreviousSignal = Semaphore->Header.SignalState;

    // Publish the released permits while holding the dispatcher lock.
    Semaphore->Header.SignalState += Adjustment;

    // Hand available permits directly to queued waiters. Each successful claim
    // consumes one permit, up to Adjustment or until the queue is exhausted.
    PITHREAD WaitingThread = MspDequeueNextWaitThreadLocked(&Semaphore->Header.WaitListHead);

    while (WaitingThread != NULL) {
        // A zero count means every released permit has already been consumed.
        if (Semaphore->Header.SignalState == 0) break;

        // Claim the thread for its wake atomically
        if (MsClaimThreadWait(WaitingThread, MT_SUCCESS)) {
            // Consume the handed-off permit before dropping the dispatcher lock.
            // Timer removal and wait completion acquire their own locks.
            Semaphore->Header.SignalState--;
            MsReleaseSpinlock(&Semaphore->Header.Lock, dispatcherIrql);

            // Remove timer queue for this thread and complete its wait.
            MsRemoveTimerQueue(WaitingThread);
            MsCompleteThreadWait(WaitingThread);

            // Reacquire the Semaphore dispatcher lock.
            MsAcquireSpinlock(&Semaphore->Header.Lock, &dispatcherIrql);
        }

        // Dequeue another waiter only while a permit remains available.
        if (Semaphore->Header.SignalState > 0) {
            WaitingThread = MspDequeueNextWaitThreadLocked(&Semaphore->Header.WaitListHead);
        }
        else {
            WaitingThread = NULL;
        }
    }

    // Release the lock.
    MsReleaseSpinlock(&Semaphore->Header.Lock, dispatcherIrql);

    // Return the permit count observed before this release.
    return PreviousSignal;
}
