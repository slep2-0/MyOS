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

        Initializes a MUTEX object, the MUTEX must be in resident memory.

    Arguments:

        Pointer to MUTEX object.

    Return Values:

        Various MTSTATUS Codes. 

--*/

{

    // Start of function
    if (!mut) return MT_INVALID_ADDRESS;

    bool isValid = MmIsAddressPresent((uintptr_t)mut);
    assert((isValid) == 1, "MUTEX Pointer given to function isn't paged in.");
    if (!isValid) {
        return MT_INVALID_ADDRESS;
    }

    IRQL oldirql;
    MsAcquireSpinlock(&mut->lock, &oldirql);

    assert((mut->ownerTid) == 0, "Mutex must not be owned already in initialization.");
    if (mut->ownerTid) {
        MsReleaseSpinlock(&mut->lock, oldirql);
        return MT_MUTEX_ALREADY_OWNED;
    }

    mut->ownerTid = 0;
    mut->locked = false;
    mut->ownerThread = NULL;

    // Initialize the event state (event->lock is separate and must be preallocated)
    // Initialize waiting queue under event lock for safety
    {
        IRQL eflags;
        MsAcquireSpinlock(&mut->SynchEvent.lock, &eflags);
        mut->SynchEvent.type = SynchronizationEvent;
        mut->SynchEvent.signaled = false;
        mut->SynchEvent.waitingQueue.head = mut->SynchEvent.waitingQueue.tail = NULL;
        MsReleaseSpinlock(&mut->SynchEvent.lock, eflags);
    }

    MsReleaseSpinlock(&mut->lock, oldirql);
    return MT_SUCCESS;
}

MTSTATUS 
MsAcquireMutexObject (
    IN  PMUTEX mut
) 

/*++

    Routine description : Acquires a MUTEX for the current thread.

    Arguments:

        Pointer to MUTEX object.

    Return Values:

        MTSTATUS Code.

    Note:
        
        This function MUST NOT be called when IRQL is equal or higher than DISPATCH_LEVEL.

--*/

{
    // Check parameter.
    if (!mut) return MT_INVALID_ADDRESS;
    // Check if address is currently non pageable in memory.
    if (!MmIsAddressPresent((uintptr_t)mut)) {
        return MT_INVALID_ADDRESS;
    }

    IRQL mflags;
    assert((MeGetCurrentIrql() < DISPATCH_LEVEL), "Blocking code called at DISPATCH_LEVEL or higher IRQL.");

    for (;;) {
        MsAcquireSpinlock(&mut->lock, &mflags);
        PETHREAD currThread = PsGetCurrentThread();

        if (!mut->locked) {
            mut->locked = true;
            mut->ownerTid = currThread->TID;
            mut->ownerThread = currThread;
            MsReleaseSpinlock(&mut->lock, mflags);
#ifdef DEBUG
            gop_printf(COLOR_RED, "[MUTEX-DEBUG] Mutex successfully acquired by: %p. MUT: %p\n", currThread, mut);
#endif
            return MT_SUCCESS;
        }

        /* mutex is locked -> enqueue/wait */
#ifdef DEBUG
        gop_printf(COLOR_RED, "[MUTEX-DEBUG] Mutex busy, enqueuing: MUT: %p\n", mut);
#endif
        /* Enqueue under the event lock inside MsWaitForEvent; release mut->lock first */
        MsReleaseSpinlock(&mut->lock, mflags);

        MsWaitForEvent(&mut->SynchEvent);

        /* When MsWaitForEvent returns we loop and try again atomically */
    }
}

MTSTATUS 
MsReleaseMutexObject (
    IN  PMUTEX mut
) 

/*++

    Routine description : Releases a MUTEX object, wakes all threads waiting on it (nonblocking).

    Arguments:

        Pointer to MUTEX object.

    Return Values:

        MTSTATUS Code.

--*/

{

    // Start of function
    if (!mut) return MT_INVALID_ADDRESS;

    // FOLLOW LOCK ORDER: acquire mut->lock then event->lock
    IRQL mflags;
    MsAcquireSpinlock(&mut->lock, &mflags);

    assert((mut->ownerTid) != 0, "Attempted release of mutex when it has no owner.");
    if (!mut->ownerTid) {
        MsReleaseSpinlock(&mut->lock, mflags);
        return MT_MUTEX_NOT_OWNED;
    }

    // Clear ownership while still holding the spinlock
    mut->ownerTid = 0;
    mut->locked = false;
    mut->ownerThread = NULL;

    MsReleaseSpinlock(&mut->lock, mflags);

    // Wake the selected thread by setting an event.
    MsSetEvent(&mut->SynchEvent);

    return MT_SUCCESS;
}