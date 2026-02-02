/*++

Module Name:

    pushlock.c

Purpose:

    This translation unit contains the implementation of kernel push lock synchronization.

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "../../includes/ms.h"
#include "../../intrinsics/atomic.h"
#include "../../includes/mm.h"

static
void
MspSuspendPushLock(
    IN PUSH_LOCK* Lock,
    IN PUSH_LOCK_WAIT_BLOCK* WaitBlock,
    IN uint64_t CurrentValue
)
{
    //  We use SynchronizationEvent because we want to wake 1 waiter at a time.
    WaitBlock->WakeEvent.type = SynchronizationEvent;
    WaitBlock->WakeEvent.signaled = false;
    WaitBlock->WakeEvent.lock.locked = 0;

    // Initialize list head.
    InitializeListHead((PDOUBLY_LINKED_LIST)&WaitBlock->WakeEvent.waitingQueue);

    WaitBlock->Signaled = false;
    WaitBlock->ShareCount = 0;

    // If the lock currently has readers (Shared Count > 0) and is not a pointer to a wait list yet,
    // we must save that count so we don't lose track of those readers.
    if ((CurrentValue & ~PL_FLAG_MASK) > 0 && !(CurrentValue & PL_WAIT_BIT)) {
        WaitBlock->ShareCount = (uint32_t)(CurrentValue >> 4);
    }

    // Push waitblock to head of &Lock->Value
    while (true) {
        // If the Waiting bit is set, the value is a pointer to the current head
        if (CurrentValue & PL_WAIT_BIT) {
            WaitBlock->Next = (PUSH_LOCK_WAIT_BLOCK*)(CurrentValue & ~PL_FLAG_MASK);
        }
        else {
            // No waiters yet. Next is NULL.
            WaitBlock->Next = NULL;
        }

        // Calculate new value: Pointer to Us | Waiting Bit | Lock Bit
        // We keep the Lock Bit set to prevent new fast path acquires while we wait.
        uint64_t NewValue = (uint64_t)WaitBlock | PL_WAIT_BIT | PL_LOCK_BIT;

        // Atomic Swap
        uint64_t Result = InterlockedCompareExchangeU64(&Lock->Value, NewValue, CurrentValue);

        if (Result == CurrentValue) {
            // Success
            break;
        }

        // Failed (value changed by another core), retry.
        CurrentValue = Result;
    }

    // We are now in the queue. We wait for our WakeEvent to be signaled by the releaser.
    MsWaitForEvent(&WaitBlock->WakeEvent);
}

void
MsAcquirePushLockExclusive(
    IN PUSH_LOCK* Lock
)
{
    // If nobody owns an exclusive lock, we return and set it to owned.
    if (InterlockedCompareExchangeU64(&Lock->Value, PL_LOCK_BIT, 0) == 0) {
        return;
    }

    // Allocate a wait block.
    PUSH_LOCK_WAIT_BLOCK* WaitBlock = (PUSH_LOCK_WAIT_BLOCK*)MmAllocatePoolWithTag(NonPagedPool, sizeof(PUSH_LOCK_WAIT_BLOCK), 'tiaw');
    if (!WaitBlock) return; // idk what to do here to be honest.

    // An exclusive lock is owned.., we just push to the waitblock.
    WaitBlock->Flags = PL_FLAGS_EXCLUSIVE;
    MspSuspendPushLock(Lock, WaitBlock, Lock->Value);
}

void
MsReleasePushLockExclusive(
    IN PUSH_LOCK* Lock
)
{
    uint64_t Value, NewValue;
    
    // If the value is the bit, we just set to 0 (no waiters exist)
    if (InterlockedCompareExchangeU64(&Lock->Value, 0, PL_LOCK_BIT) == PL_LOCK_BIT) {
        return;
    }

    // Waiters exist, we must release them.
    while (true) {
        Value = Lock->Value;

        // If somebody cleared the lock bit but left waiters? (shouldn't happen in standard flow but safe to check)
        if (!(Value & PL_WAIT_BIT)) {
            InterlockedAndU64(&Lock->Value, ~PL_LOCK_BIT);
            return;
        }

        // Get the list head
        PUSH_LOCK_WAIT_BLOCK* Head = (PUSH_LOCK_WAIT_BLOCK*)(Value & ~PL_FLAG_MASK);
        
        // Pop next item
        PUSH_LOCK_WAIT_BLOCK* Next = Head->Next;

        // Calculate New Value
        // If Next is NULL, we clear the Waiting bit.
        NewValue = (uint64_t)Next;
        if (NewValue != 0) NewValue |= PL_WAIT_BIT;

        // If ther waiter we are waking is an exclusive waiter, we can leave the bit, its an optimization.
        if (Head->Flags == PL_FLAGS_EXCLUSIVE) {
            NewValue |= PL_LOCK_BIT;
        }

        // Try to update the lock pointer
        if (InterlockedCompareExchangeU64(&Lock->Value, NewValue, Value) == Value) {
            // Wake the next waiter.
            MsSetEvent(&Head->WakeEvent);
            
            // Free the waiters memory allocated.
            MmFreePool(Head);

            return;
        }
    }
}

void
MsAcquirePushLockShared(
    IN PUSH_LOCK* Lock
)
{
    uint64_t Value, NewValue;

    while (true) {
        Value = Lock->Value;

        // If Locked (Bit 0) or Waiting (Bit 1) is set, we must wait.
        // We just sleep.
        if (Value & (PL_LOCK_BIT | PL_WAIT_BIT)) {
            PUSH_LOCK_WAIT_BLOCK* WaitBlock = (PUSH_LOCK_WAIT_BLOCK*)MmAllocatePoolWithTag(NonPagedPool, sizeof(PUSH_LOCK_WAIT_BLOCK), 'tiaw');
            if (!WaitBlock) return; // idk what to do here to be honest.
            WaitBlock->Flags = PL_FLAGS_SHARED;
            MspSuspendPushLock(Lock, WaitBlock, Value);
            return;
        }

        // Increment share count, no one is locking or waiting.
        NewValue = Value + PL_SHARE_INC;

        if (InterlockedCompareExchangeU64(&Lock->Value, NewValue, Value) == Value) {
            return;
        }
    }
}

void
MsReleasePushLockShared(
    IN PUSH_LOCK* Lock
)
{
    uint64_t Value, NewValue;

    while (true) {
        Value = Lock->Value;

        // Check if there are waiters
        if (Value & PL_WAIT_BIT) {
            // If someone is waiting, the Value is no longer a count of shared threads, but a pointer to setting the event to wake the exclusive waiter.
            // If no more shares, we wake up the waiter and he acquires the exclusive lock.

            PUSH_LOCK_WAIT_BLOCK* Head = (PUSH_LOCK_WAIT_BLOCK*)(Value & ~PL_FLAG_MASK);
            PUSH_LOCK_WAIT_BLOCK* Last = Head;

            // Find the end of the chain (TODO LAST PTR TO MAKE IT FASTER (HINT))
            while (Last->Next != NULL) {
                Last = Last->Next;
            }

            // Decrement the saved count in the wait block
            if (InterlockedDecrementU32(&Last->ShareCount) == 0) {
                // If we hit zero, we signal the writer that he can stop waiting for readers.
                MsSetEvent(&Last->WakeEvent);
            }
            return;
        }

        // Decrement shared count.
        NewValue = Value - PL_SHARE_INC;

        if (InterlockedCompareExchangeU64(&Lock->Value, NewValue, Value) == Value) {
            return;
        }
    }
}