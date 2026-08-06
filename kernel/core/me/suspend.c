/*++

Module Name:

    suspend.c

Purpose:

    This translation unit contains the implementation of thread suspension and resume.

Author:

    slep (Matanel) 2026.

Revision History:

--*/

#include "../../includes/me.h"
#include "../../assert.h"

MTSTATUS
MeSuspendThread(
    IN PITHREAD Thread,
    OUT uint32_t* PreviousSuspendCount
)

{
    // Asserion to be less or equal than dispatch, since we acquire a spinlock.
    assert(MeGetCurrentIrql() <= DISPATCH_LEVEL);

    // ApcQueueLock serializes SuspendCount with SuspendAPC queue membership.
    // Every suspend/resume transition must use this same lock.
    IRQL oldIrql;
    MsAcquireSpinlock(&Thread->ApcQueueLock, &oldIrql);

    if (InterlockedLoad(&Thread->ApcQueueable) == false) {
        // We cannot Queue APCs, return.
        MsReleaseSpinlock(&Thread->ApcQueueLock, oldIrql);
        return MT_THREAD_IS_TERMINATING;
    }

    if (WILL_ADD_OVERFLOW(Thread->SuspendCount, 1)) {
        // FIXME TODO Raise a status here after the spinlock release, this is an obvious infinite loop on the caller thread.
        MsReleaseSpinlock(&Thread->ApcQueueLock, oldIrql);
        return MT_SUSPEND_LIMIT_EXCEEDED;
    }

    // Record the previous logical suspend count, then add this caller's request.
    // Plain access is sufficient because every count change uses ApcQueueLock.
    uint32_t OldValue = Thread->SuspendCount++;
    PPROCESSOR SavedProcessor = NULL;

    if (OldValue == 0) {
        /*
         * A 0 -> 1 transition begins a new logical suspension period. Close
         * the private resume gate first so a permit left by an earlier resume
         * cannot satisfy this new suspension request.
         *
         * SignalState may already be zero. That happens when the previous APC
         * was waiting and consumed the resume permit directly. It may be one
         * when resume ran before the APC reached its wait. Both states are
         * valid while SuspendApcActive is still true.
         *
         * ApcQueueLock -> SuspendSemaphore.Header.Lock is the required lock
         * order for every path that needs both locks.
         */
        IRQL dispatcherIrql;
        MsAcquireSpinlock(
            &Thread->SuspendSemaphore.Header.Lock,
            &dispatcherIrql
        );
        assert(Thread->SuspendSemaphore.Header.SignalState >= 0);
        assert(Thread->SuspendSemaphore.Header.SignalState <= 1);
        Thread->SuspendSemaphore.Header.SignalState = 0;
        MsReleaseSpinlock(
            &Thread->SuspendSemaphore.Header.Lock,
            dispatcherIrql
        );

        /*
         * SuspendApcActive covers the complete APC invocation, including the
         * interval after Inserted is cleared but before the normal routine has
         * finished. Reuse that invocation when possible; its completion loop
         * will observe the new positive SuspendCount and wait again.
         */
        if (!Thread->SuspendApcActive) {
            // No invocation owns suspension, so the embedded APC must be free.
            assert(Thread->SuspendAPC.Inserted == 0);

            // SavedProcessor is the CPU to notify after ApcQueueLock is released.
            bool Inserted = MepInsertQueueApcLocked(
                &Thread->SuspendAPC,
                NULL,
                NULL,
                &SavedProcessor
            );
            
            if (!Inserted) {
                // Restore the logical state because no APC can enforce it.
                Thread->SuspendCount = OldValue;
                MsReleaseSpinlock(&Thread->ApcQueueLock, oldIrql);
                return MT_APC_ERROR;
            }

            Thread->SuspendApcActive = true;
        }
    }

    *PreviousSuspendCount = OldValue;
    MsReleaseSpinlock(&Thread->ApcQueueLock, oldIrql);

    if (SavedProcessor != NULL) {
        if (SavedProcessor == MeGetCurrentProcessor()) {
            // Call an APC Interrupt on this CPU.
            bool InterruptsEnabled = MeDisableInterrupts();
            MhRequestSoftwareInterrupt(APC_LEVEL);
            MeEnableInterrupts(InterruptsEnabled);
        }
        else {
            // Request an IPI for it
            IPI_PARAMS params = { 0 };
            MhSendActionToSpecificCpuAndWait(SavedProcessor, CPU_ACTION_REQUEST_APC, params);
        }
    }

    return MT_SUCCESS;
}

MTSTATUS
MeResumeThread(
    IN PITHREAD Thread,
    OUT uint32_t* PreviousSuspendCount
)

{
    // Asserion to be less or equal than dispatch, since we acquire a spinlock.
    assert(MeGetCurrentIrql() <= DISPATCH_LEVEL);

    // Acquire APC Queue lock.
    IRQL oldIrql;
    MsAcquireSpinlock(&Thread->ApcQueueLock, &oldIrql);

    if (InterlockedLoad(&Thread->ApcQueueable) == false) {
        // We cannot queue APCs, return.
        MsReleaseSpinlock(&Thread->ApcQueueLock, oldIrql);
        return MT_THREAD_IS_TERMINATING;
    }

    if (Thread->SuspendCount == 0) {
        // Nothing to resume.
        MsReleaseSpinlock(&Thread->ApcQueueLock, oldIrql);
        *PreviousSuspendCount = 0;
        return MT_SUCCESS;
    }

    uint32_t OldValue = Thread->SuspendCount--;

    if (OldValue == 1) {
        // We should resume the thread now.
        MsReleaseSemaphore(&Thread->SuspendSemaphore, 1);
    }

    *PreviousSuspendCount = OldValue;
    MsReleaseSpinlock(&Thread->ApcQueueLock, oldIrql);
    return MT_SUCCESS;
}
