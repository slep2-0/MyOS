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
#include "../../includes/me.h"
#include "../../includes/mg.h"
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

MTSTATUS
MsReleaseSemaphoreChecked(
    IN PSEMAPHORE Semaphore,
    IN int32_t Adjustment,
    _Out_Opt int32_t* PreviousCount
)

/*++

    Routine description:

        Releases a Semaphore object.

    Arguments:

        [IN]    Semaphore - Semaphore whose permit count is increased.
        [IN]    Adjustment - Positive number of permits to release.


    Return Values:

        MT_SUCCESS on success, or MT_INVALID_ADDRESS, MT_INVALID_PARAM, or
        MT_SEMAPHORE_LIMIT_EXCEEDED when validation fails.

--*/

{
    if (!Semaphore) return MT_INVALID_ADDRESS;
    if (Adjustment <= 0) return MT_INVALID_PARAM;

    // Lock the dispatcher object.
    IRQL dispatcherIrql;
    MsAcquireSpinlock(&Semaphore->Header.Lock, &dispatcherIrql);

    // Perform some important runtime assesments first.
    // The caller must not provide an adjustment that would go over the limit.
    // Also make it immune to signed overflow.
    if (Adjustment > Semaphore->Limit - Semaphore->Header.SignalState) {
        MsReleaseSpinlock(&Semaphore->Header.Lock, dispatcherIrql);
        return MT_SEMAPHORE_LIMIT_EXCEEDED;
    }

    // Save the previous permit count for the return value.
    int32_t PreviousSignal = Semaphore->Header.SignalState;
    if (PreviousCount) {
        *PreviousCount = PreviousSignal;
    }

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
            MeBoostThread(WaitingThread, MT_SYNCHRONIZATION_BOOST);
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

    return MT_SUCCESS;
}

int32_t
MsReleaseSemaphore(
    IN PSEMAPHORE Semaphore,
    IN int32_t Adjustment
)

/*++

    Routine description:

        Releases permits from a semaphore and returns its previous count.

    Arguments:

        [IN OUT] Semaphore - The semaphore to adjust.
        [IN] Adjustment - The positive number of permits to release.

    Return Values:

        The previous permit count. A limit violation is treated as a kernel
        bug and bugchecks after the checked helper reports failure.

--*/
{
    int32_t PreviousCount = 0;

    // Its okay to bugcheck on failure, since user mode semaphores are pre-checked by the Syscall handler
    // So bugchecking would mean kernel/driver failure
    MTSTATUS Status = MsReleaseSemaphoreChecked(
        Semaphore,
        Adjustment,
        &PreviousCount
    );
    if (MT_FAILURE(Status)) {
        MeBugCheckEx(
            SEMAPHORE_LIMIT_REACHED,
            Semaphore,
            (void*)(uintptr_t)Adjustment,
            MeGetCurrentThread(),
            (void*)(uintptr_t)Status
        );
    }
    return PreviousCount;
}
