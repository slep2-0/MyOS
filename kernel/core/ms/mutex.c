/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     GPLv3
 * PURPOSE:     Mutex Implementation.
 */

#include "../../includes/me.h"
#include "../../includes/ps.h"
#include "../../includes/mg.h"
#include "../../assert.h"

MTSTATUS 
MsInitializeMutexObject (
    IN  PMUTEX mut
) 

/*++

    Routine description : 

        Initializes a MUTEX object, the MUTEX must be in resident memory, see notes to why.

    Arguments:

        Pointer to MUTEX object.

    Return Values:

        Various MTSTATUS Codes. 

    Notes:

        Why are mutexes in resident memory? Like Events, Semaphores, and even Threads or Processes, even if you wait on them (which requires APC_LEVEL or below)
        They still must be in resident memory at all times, since higher level of IRQL (DISPATCH_LEVEL, for example DPCs, or even CLOCK) may touch the dispatcher object
        To lets say for example remove it from Timer queue (since he could have waited with a timeout)

--*/

{
    if (!mut) return MT_INVALID_ADDRESS;


    // The caller supplies raw resident storage. Acquiring either embedded lock
    // before initializing it can spin forever on stale pool contents. ALL THOUGH it shouldnt, since we kmemset the pool.
    kmemset(mut, 0, sizeof(*mut));

    // SignalState starts with 1, meaning available mutex.
    MsInitializeDispatcherHeader(&mut->Header, 1, DispatcherMutex);
    mut->Abandoned = false;
    mut->OwnerThread = NULL;
    InitializeListHead(&mut->OwnerListEntry);
    return MT_SUCCESS;
}

//
//MTSTATUS 
//MsAcquireMutexObject (
//    IN  PMUTEX mut
//) 
//
///*++
//
//    Routine description : Acquires a MUTEX for the current thread.
//
//    Arguments:
//
//        Pointer to MUTEX object.
//
//    Return Values:
//
//        MTSTATUS Code.
//
//    Note:
//        
//        This function MUST NOT be called when IRQL is equal or higher than DISPATCH_LEVEL.
//
//--*/
//
//{
//    // Check parameter.
//    if (!mut) return MT_INVALID_ADDRESS;
//    // Check if address is currently non pageable in memory.
//    if (!MmIsAddressPresent((uintptr_t)mut)) {
//        return MT_INVALID_ADDRESS;
//    }
//
//    IRQL mflags;
//    assert((MeGetCurrentIrql() < DISPATCH_LEVEL), "Blocking code called at DISPATCH_LEVEL or higher IRQL.");
//
//    for (;;) {
//        MsAcquireSpinlock(&mut->lock, &mflags);
//        PETHREAD currThread = PsGetCurrentThread();
//
//        if (mut->locked && mut->ownerThread == currThread) {
//            MsReleaseSpinlock(&mut->lock, mflags);
//            return MT_MUTEX_ALREADY_OWNED;
//        }
//
//        if (!mut->locked) {
//            mut->locked = true;
//            mut->ownerTid = currThread->TID;
//            mut->ownerThread = currThread;
//            MsReleaseSpinlock(&mut->lock, mflags);
//#ifdef DEBUG
//            gop_printf(COLOR_RED, "[MUTEX-DEBUG] Mutex successfully acquired by: %p. MUT: %p\n", currThread, mut);
//#endif
//            return MT_SUCCESS;
//        }
//
//        /* mutex is locked -> enqueue/wait */
//#ifdef DEBUG
//        gop_printf(COLOR_RED, "[MUTEX-DEBUG] Mutex busy, enqueuing: MUT: %p\n", mut);
//#endif
//        /* Enqueue under the event lock inside MsWaitForEvent; release mut->lock first */
//        MsReleaseSpinlock(&mut->lock, mflags);
//
//        MsWaitForEvent(&mut->SynchEvent, INFINITE);
//
//        /* When MsWaitForEvent returns we loop and try again atomically */
//    }
//}


MTSTATUS 
MsReleaseMutexObject (
    IN  PMUTEX Mutex
) 

/*++

    Routine description : Releases one level of mutex ownership. A final
                          release transfers ownership to at most one waiter.

    Arguments:

        Pointer to MUTEX object.

    Return Values:

        MTSTATUS Code.

--*/

{

    // Start of function
    assert(Mutex != NULL);

    // Acquire Dispatcher Lock
    IRQL dispatcherIrql;
    MsAcquireSpinlock(&Mutex->Header.Lock, &dispatcherIrql);

    // Reject release of an already available mutex.
    if (Mutex->Header.SignalState == 1) {
        // No thread owns this mutex.
        MsReleaseSpinlock(&Mutex->Header.Lock, dispatcherIrql);
        return MT_MUTEX_NOT_OWNED;
    }

    PETHREAD CurrentThread = PsGetCurrentThread();
    if (CurrentThread != Mutex->OwnerThread) {
        // Only the current owner may release a mutex.
        MsReleaseSpinlock(&Mutex->Header.Lock, dispatcherIrql);
        return MT_MUTEX_NOT_OWNED;
    }
    
    // Each release reverses one acquisition. Values at or below zero still
    // represent an owned mutex; only the transition to one fully releases it.
    assert(Mutex->Header.SignalState <= 0);
    Mutex->Header.SignalState++;

    if (Mutex->Header.SignalState <= 0) {
        MsReleaseSpinlock(&Mutex->Header.Lock, dispatcherIrql);
        return MT_SUCCESS;
    }

    assert(Mutex->Header.SignalState == 1);

    // Final release removes the mutex from the old owner's list.
    MsAcquireSpinlockAtDpcLevel(&Mutex->OwnerThread->InternalThread.OwnedMutexesListLock);
    RemoveEntryList(&Mutex->OwnerListEntry);
    InitializeListHead(&Mutex->OwnerListEntry);
    MsReleaseSpinlockFromDpcLevel(&Mutex->OwnerThread->InternalThread.OwnedMutexesListLock);

    // Clear the old owner before either handing off or leaving the mutex free.
    Mutex->OwnerThread = NULL;

    // Directly transfer the released mutex to the first waiter whose wait we
    // can claim. Timed-out or terminating waiters have already lost the CAS.
    PITHREAD NextThread = MspDequeueNextWaitThreadLocked(
        &Mutex->Header.WaitListHead
    );

    while (NextThread != NULL) {
        MTSTATUS CompletionStatus = Mutex->Abandoned
            ? MT_MUTEX_ABANDONED
            : MT_SUCCESS;

        if (MsClaimThreadWait(NextThread, CompletionStatus)) {
            // The waiter returns from MsWaitForSingleObject already owning the
            // mutex. No third thread can take it between wake and dispatch.
            Mutex->Header.SignalState = 0;
            Mutex->OwnerThread = PsGetEThreadFromIThread(NextThread);

            // Link the mutex into the replacement owner's list before wake completion.
            MsAcquireSpinlockAtDpcLevel(&NextThread->OwnedMutexesListLock);
            InsertTailList(&NextThread->OwnedMutexListHead, &Mutex->OwnerListEntry);
            MsReleaseSpinlockFromDpcLevel(&NextThread->OwnedMutexesListLock);

            Mutex->Abandoned = false;

            MsReleaseSpinlock(&Mutex->Header.Lock, dispatcherIrql);
            MsRemoveTimerQueue(NextThread);
            MsCompleteThreadWait(NextThread);
            return MT_SUCCESS;
        }

        NextThread = MspDequeueNextWaitThreadLocked(
            &Mutex->Header.WaitListHead
        );
    }

    MsReleaseSpinlock(&Mutex->Header.Lock, dispatcherIrql);
    return MT_SUCCESS;
}
