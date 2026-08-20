/*++

Module Name:

    pushlock.c

Purpose:

    Small non-allocating reader/writer lock used by executive data structures.

--*/

#include "../../includes/ms.h"
#include "../../includes/me.h"
#include "../../includes/mh.h"
#include "../../intrinsics/atomic.h"
#include "../../assert.h"
/*

You must enter this function with the push lock spinlock locked.

*/
static
void
MspQueuePushLockWaiterLocked(
    IN PPUSH_LOCK PushLock,
    IN PPUSH_LOCK_WAIT_BLOCK Waiter
)

/*++

    Routine description:

        Appends a push-lock waiter to the protected FIFO queue.

    Arguments:

        [IN OUT] PushLock - The push lock whose queue is protected.
        [IN OUT] Waiter - The stack wait block to append.

    Return Values:

        None.

    Notes:

        The caller must hold PushLock->StateLock.

--*/

{
    // Next waiter is initially NULL.
    Waiter->Next = NULL;

    // Initialize WaitTail->Next
    if (PushLock->WaitTail) {
        PushLock->WaitTail->Next = Waiter;
    }
    else {
        // Else, the waiter is the first one.
        PushLock->WaitHead = Waiter;
    }

    // Always set the wait tail as this waiter.
    PushLock->WaitTail = Waiter;
}

static
void
MspWakePushLockWaiters(
    IN PPUSH_LOCK_WAIT_BLOCK WaitHead
)

/*++

    Routine description:

        Signals a detached list of push-lock waiters and publishes completion
        after the waker no longer accesses each stack wait block.

    Arguments:

        [IN] WaitHead - The detached waiter list.

    Return Values:

        None.

--*/

{
    while (WaitHead != NULL) {
        // Save the next stack wait block before publishing WakeComplete. Once
        // published, this waiter may return and destroy its local wait block.
        PPUSH_LOCK_WAIT_BLOCK Next = WaitHead->Next;

        MTSTATUS Status = MsSetEvent(&WaitHead->WakeEvent);
        assert(Status == MT_SUCCESS);
        (void)Status;

        InterlockedStoreRelease(&WaitHead->WakeComplete, true);

        // Move over to next one if any.
        WaitHead = Next;
    }
}

static
PPUSH_LOCK_WAIT_BLOCK
MspGrantPushLockWaitersLocked(
    IN PPUSH_LOCK PushLock
)

/*++

    Routine description:

        Transfers an unowned push lock to the first eligible exclusive waiter
        or to the consecutive shared waiters at the queue head.

    Arguments:

        [IN OUT] PushLock - The push lock whose queue is protected.

    Return Values:

        The detached waiter list to wake, or NULL when no waiter is queued.

    Notes:

        The caller must hold PushLock->StateLock and must wake the returned
        list after releasing that lock.

--*/

{
    // Remove the selected waiter from PushLock
    // There must not be an owner for this pushlock when granting on release
    assert(PushLock);
    assert(PushLock->ExclusiveOwned == false);
    assert(PushLock->SharedOwners == 0);

    PPUSH_LOCK_WAIT_BLOCK WaitHead = PushLock->WaitHead;

    if (WaitHead == NULL) {
        assert(PushLock->WaitTail == NULL);
        return NULL;
    }

    // If the push lock head is an EXCLUSIVE writer/reader, then we release him ONLY and return.
    if (WaitHead->Mode == PushLockWaitExclusive) {
        // Pop him out.
        PushLock->WaitHead = WaitHead->Next;

        // If there is no next waiter, clear the tail of the singly linked queue.
        if (PushLock->WaitHead == NULL) {
            PushLock->WaitTail = NULL;
        }

        // Detach his links
        WaitHead->Next = NULL;
        
        // Set him as the owner
        PushLock->ExclusiveOwned = true;

        InterlockedStoreRelease(&WaitHead->Granted, true);

        // The caller will now wake this head only.
        return WaitHead;
    }

    // The head is a reader, release any more consecutive readers UNTIL we find an exclusive writer/reader, then
    // we stop releasing, the readers behind the exclusive writer will wait.
    PPUSH_LOCK_WAIT_BLOCK Current = WaitHead;
    PPUSH_LOCK_WAIT_BLOCK Tail = NULL;

    while (Current && Current->Mode == PushLockWaitShared) {
        assert(PushLock->SharedOwners != UINT32_MAX);

        // For each shared owner, increment the shared owners int
        PushLock->SharedOwners++;

        // For each owner, store Granted as true
        InterlockedStoreRelease(&Current->Granted, true);

        // The tail is now the previous (Current) one, while the next one is now Current
        Tail = Current;
        Current = Current->Next;
    }

    // Finished releasing all waiters, we now return the wait head of this one too
    // We do not clear its links, the caller must set the event for each waiter in this list
    PushLock->WaitHead = Current;

    if (!Current) {
        PushLock->WaitTail = NULL;
    }

    // Separate the selected wake list from the rest of the remaining push lock queue
    // So the caller wont wake them too
    Tail->Next = NULL;

    // Return the head now
    return WaitHead;
}

void
MsInitializePushLock(
    IN PPUSH_LOCK PushLock
)

/*++

    Routine description:

        Initializes a push lock in its unowned state.

    Arguments:

        [OUT] PushLock - The push lock storage to initialize.

    Return Values:

        None.

--*/

{
    assert(PushLock != NULL);

    PushLock->StateLock.locked = 0;
    PushLock->WaitHead = NULL;
    PushLock->WaitTail = NULL;
    PushLock->SharedOwners = 0;
    PushLock->ExclusiveOwned = false;
}

void
MsAcquirePushLockExclusive(
    IN PUSH_LOCK* Lock
)

/*++

    Routine description:

        Acquires a push lock exclusively, waiting when another owner exists.

    Arguments:

        [IN OUT] Lock - The push lock to acquire.

    Return Values:

        None. The routine returns after exclusive ownership is granted.

    Notes:

        The routine may block and must be called at APC_LEVEL or below.

--*/
{
    assert(Lock != NULL);
    assert(MeGetCurrentIrql() <= APC_LEVEL,
        "Push locks may wait and cannot be acquired at DISPATCH_LEVEL.");

    // Initialize stack based wait block.
    PUSH_LOCK_WAIT_BLOCK WaitBlock = { 0 };
    MsInitializeEvent(&WaitBlock.WakeEvent, DispatcherSynchronizationEvent, false);
    WaitBlock.Mode = PushLockWaitExclusive;

    MeEnterCriticalRegion();

    // Acquire the spinlock for the push lock
    IRQL prevIrql;
    MsAcquireSpinlock(&Lock->StateLock, &prevIrql);

    // When the lock is completely free and empty
    if (Lock->ExclusiveOwned == false && Lock->SharedOwners == 0 && Lock->WaitHead == NULL) {
        Lock->ExclusiveOwned = true;

        MsReleaseSpinlock(&Lock->StateLock, prevIrql);
        return;
    }

    // Pre-thread boot code may use an uncontended push lock, but it cannot
    // enqueue and block because no schedulable thread exists yet.
    assert(MeGetCurrentThread() != NULL,
        "A push lock cannot block before a current thread exists.");

    // Call the helper to set us to the tail
    MspQueuePushLockWaiterLocked(Lock, &WaitBlock);

    // Release the lock now, we are in the queue
    MsReleaseSpinlock(&Lock->StateLock, prevIrql);

    // Wait for the push lock release to wake us up.
    MTSTATUS Status = MsWaitForSingleObject(&WaitBlock.WakeEvent, KernelMode, false, MT_INFINITE);
    assert(MT_SUCCEEDED(Status));
    (void)Status;

    // A success status does not mean that the wait is complete, we must SPIN until WaitComplete is true
    // because the releaser might be touching the stack (releaser might be still in MsSetEvent while we return; and so destroy the stack of the WaitBlock he is still supposed to touch)
    while (InterlockedLoadAcquire(&WaitBlock.WakeComplete) == false) {
        MhSpinAndProcessIpis();
    }
    
    // If someone wakes us up without granted, then its a kernel bug.
    assert(InterlockedLoadAcquire(&WaitBlock.Granted));

    return;
}

void
MsReleasePushLockExclusive(
    IN PUSH_LOCK* Lock
)

/*++

    Routine description:

        Releases exclusive ownership and wakes the next eligible waiters.

    Arguments:

        [IN OUT] Lock - The exclusively owned push lock.

    Return Values:

        None.

--*/
{
    assert(Lock != NULL);

    // Acquire the lock
    IRQL prevIrql;
    MsAcquireSpinlock(&Lock->StateLock, &prevIrql);

    // Release exclusive owned and grant 1 exclusive waiter, or every consecutive shared waiter until we see another exclusive waiter, then we stop.
    assert(Lock->ExclusiveOwned && Lock->SharedOwners == 0);
    Lock->ExclusiveOwned = false;
    PPUSH_LOCK_WAIT_BLOCK WaitHead = MspGrantPushLockWaitersLocked(Lock);

    // Release the lock now, we must wake them up.
    MsReleaseSpinlock(&Lock->StateLock, prevIrql);

    // Wake up every waiter in WaitHead.
    MspWakePushLockWaiters(WaitHead);

    // Leave the critical region now
    MeLeaveCriticalRegion();
}

void
MsAcquirePushLockShared(
    IN PUSH_LOCK* Lock
)

/*++

    Routine description:

        Acquires a push lock for shared ownership, waiting behind an exclusive
        owner or queued writer.

    Arguments:

        [IN OUT] Lock - The push lock to acquire.

    Return Values:

        None. The routine returns after shared ownership is granted.

    Notes:

        The routine may block and must be called at APC_LEVEL or below.

--*/
{
    assert(Lock != NULL);
    assert(MeGetCurrentIrql() <= APC_LEVEL,
        "Push locks may wait and cannot be acquired at DISPATCH_LEVEL.");

    MeEnterCriticalRegion();

    IRQL prevIrql;
    MsAcquireSpinlock(&Lock->StateLock, &prevIrql);

    // If the push lock isn't owned by an exclusive owner, then we may acquire the shared one too.
    // Else, we would wait until granted and release.
    if (Lock->ExclusiveOwned == false && Lock->WaitHead == NULL) {
        assert(WILL_ADD_OVERFLOW(Lock->SharedOwners, 1) == false);
        Lock->SharedOwners++;
        MsReleaseSpinlock(&Lock->StateLock, prevIrql);
        return;
    }

    // Pre-thread boot code may use an uncontended push lock, but it cannot
    // enqueue and block because no schedulable thread exists yet.
    assert(MeGetCurrentThread() != NULL,
        "A push lock cannot block before a current thread exists.");

    // We must wait, initialize the wait block and insert ourselves to the tail.
    PUSH_LOCK_WAIT_BLOCK WaitBlock = { 0 };
    WaitBlock.Mode = PushLockWaitShared;
    MsInitializeEvent(&WaitBlock.WakeEvent, DispatcherSynchronizationEvent, false);
    MspQueuePushLockWaiterLocked(Lock, &WaitBlock);

    // Release the lock before sleeping
    MsReleaseSpinlock(&Lock->StateLock, prevIrql);

    MTSTATUS Status = MsWaitForSingleObject(&WaitBlock.WakeEvent, KernelMode, false, MT_INFINITE);
    assert(MT_SUCCEEDED(Status));
    (void)Status;

    // We have been awoken, but we cannot return yet since the waker might still need to access WaitBlock above, so until he set that he doesnt need it anymore
    while (InterlockedLoadAcquire(&WaitBlock.WakeComplete) == false) {
        MhSpinAndProcessIpis();
    }

    assert(InterlockedLoadAcquire(&WaitBlock.Granted));
}

void
MsReleasePushLockShared(
    IN PUSH_LOCK* Lock
)

/*++

    Routine description:

        Releases one shared owner and grants queued waiters when the final
        shared owner leaves.

    Arguments:

        [IN OUT] Lock - The shared-owned push lock.

    Return Values:

        None.

--*/
{
    assert(Lock != NULL);

    // Acquire the lock
    IRQL prevIrql;
    MsAcquireSpinlock(&Lock->StateLock, &prevIrql);

    assert(Lock->SharedOwners);
    assert(Lock->ExclusiveOwned == false);

    // If there are no more shared owners remaining after decrement, we must wake an exclusive waiter (if any).
    Lock->SharedOwners--;

    if (Lock->SharedOwners != 0) {
        MsReleaseSpinlock(&Lock->StateLock, prevIrql);
        MeLeaveCriticalRegion();
        return;
    }

    // Grant push lock waiters.
    PPUSH_LOCK_WAIT_BLOCK Head = MspGrantPushLockWaitersLocked(Lock);

    // Release the lock before waking
    MsReleaseSpinlock(&Lock->StateLock, prevIrql);

    // Wake the exclusive waiter if any.
    MspWakePushLockWaiters(Head);

    // Leave the critical region now
    MeLeaveCriticalRegion();
}
