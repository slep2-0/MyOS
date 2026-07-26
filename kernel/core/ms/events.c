/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:		 Events Implementation
 */

#include "../../includes/me.h"
#include "../../includes/ps.h"
#include "../../includes/mg.h"
#include "../../includes/ms.h"
#include "../../assert.h"

PITHREAD
MspDequeueNextWaitThreadLocked(
    PDOUBLY_LINKED_LIST HeaderWaitListHead
)

{
    PDOUBLY_LINKED_LIST WaiterEntry = RemoveHeadList(HeaderWaitListHead);

    if (!WaiterEntry) {
        return NULL;
    }

    PWAIT_BLOCK WaitBlock = CONTAINING_RECORD(
        WaiterEntry,
        WAIT_BLOCK,
        ObjectListEntry
    );

    PITHREAD WaitingThread = CONTAINING_RECORD(
        WaitBlock,
        ITHREAD,
        WaitBlock
    );

    // Restore the removed entry to the self-linked, unregistered state.
    InitializeListHead(&WaitingThread->WaitBlock.ObjectListEntry);

    return WaitingThread;
}

void
MsInitializeEvent(
    IN PEVENT Event,
    IN DISPATCHER_TYPE EventDispatcherType, // must be DispatcherSynchronizationEvent or DispatcherNotificationEvent
    IN bool StartSignaled
)

/*++

    Routine description :

        Initializes an EVENT object to the specified values.

    Arguments:

        Event - Pointer to EVENT object.
        EventDispatcherType - Event type (Dispatcher object type), must be DispatcherSynchronizationEvent or DispatcherNotificationEvent
        StartSignaled - Boolean value indicating if the event should start as signaled or not.

    Return Values:

        None.

--*/

{
    assert(EventDispatcherType == DispatcherSynchronizationEvent || EventDispatcherType == DispatcherNotificationEvent);
    MsInitializeDispatcherHeader(&Event->Header, StartSignaled, EventDispatcherType);
}

MTSTATUS
MsSetEventEx(
    IN PEVENT event,
    _Out_Opt bool* PreviousState
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
    assert(event->Header.Type == DispatcherSynchronizationEvent || event->Header.Type == DispatcherNotificationEvent);

    // Acquire Dispatcher lock
    IRQL prevIrql;
    MsAcquireSpinlock(&event->Header.Lock, &prevIrql);

    if (PreviousState) {
        *PreviousState = event->Header.SignalState != 0;
    }

    // Check dispatcher type
    if (event->Header.Type == DispatcherSynchronizationEvent) {
        // Release a waiter at a time.
        // We are holding the lock so its okay to manipulate the header list.
        PITHREAD WaitingThread = MspDequeueNextWaitThreadLocked(&event->Header.WaitListHead);

        while (WaitingThread != NULL) {
            // Try to claim the thread for a successful wake
            if (MsClaimThreadWait(WaitingThread, MT_SUCCESS)) {
                // This waiter consumes the synchronization event's one signal.
                // The next waiter must block until the event is set again.
                event->Header.SignalState = 0;

                // Release the lock, we dont need to hold it anymore
                MsReleaseSpinlock(&event->Header.Lock, prevIrql);

                // A fully parked BLOCKED thread becomes READY. A thread still
                // BLOCKING is completed by its owner CPU's scheduler handshake.
                // The resumed wait path cancels its remaining timer entry and
                // clears the embedded wait block before it can be reused.
                MsRemoveTimerQueue(WaitingThread);
                MsCompleteThreadWait(WaitingThread);
                return MT_SUCCESS;
            }

            // We failed to claim the thread, retry with next waiter if any
            WaitingThread = MspDequeueNextWaitThreadLocked(&event->Header.WaitListHead);
        }

        // No valid waiter consumed the signal. Store it until the next wait,
        // which consumes SignalState while holding this same lock.
        event->Header.SignalState = 1;
        MsReleaseSpinlock(&event->Header.Lock, prevIrql);
        return MT_SUCCESS;
    }
    else if (event->Header.Type == DispatcherNotificationEvent) {
        // Notification events remain signaled. Publish this before dropping
        // the lock so newly arriving waiters complete immediately instead of
        // joining the list while the existing waiters are being completed.
        event->Header.SignalState = 1;

        // Release all waiters on the event.
        PITHREAD WaitingThread = MspDequeueNextWaitThreadLocked(&event->Header.WaitListHead);

        while (WaitingThread != NULL) {
            if (MsClaimThreadWait(WaitingThread, MT_SUCCESS)) {
                // Completion can take a ready-queue lock, so it does not belong
                // under this event's dispatcher lock.
                MsReleaseSpinlock(&event->Header.Lock, prevIrql);

                // The resumed wait path owns cancellation of any remaining
                // timer entry and clears its wait block before reuse.
                MsRemoveTimerQueue(WaitingThread);
                MsCompleteThreadWait(WaitingThread);

                // Reacquire only to remove the next protected wait-list entry.
                MsAcquireSpinlock(&event->Header.Lock, &prevIrql);
            }

            WaitingThread = MspDequeueNextWaitThreadLocked(&event->Header.WaitListHead);
        }

        MsReleaseSpinlock(&event->Header.Lock, prevIrql);
        return MT_SUCCESS;
    }
    else {
        // Bad event type, bugcheck.
        MsReleaseSpinlock(&event->Header.Lock, prevIrql);
        MeBugCheckEx(
            WRONG_DISPATCHER_HEADER,
            event,
            (void*)(uintptr_t)event->Header.Type,
            MsSetEventEx,
            NULL
        );
    }
}

MTSTATUS
MsSetEvent(
    IN PEVENT Event
)
{
    return MsSetEventEx(Event, NULL);
}

bool
MsResetEvent(
    IN PEVENT Event
)

/*++

    Routine description :

        Resets an event back to its non-signaled state.

    Arguments:

        Pointer to EVENT object.

    Return Values:

        None.

--*/

{
    IRQL prevIrql;
    MsAcquireSpinlock(&Event->Header.Lock, &prevIrql);
    bool PreviousState = Event->Header.SignalState != 0;
    Event->Header.SignalState = 0;
    MsReleaseSpinlock(&Event->Header.Lock, prevIrql);
    return PreviousState;
}

///*
//MTSTATUS 
//MsWaitForEvent (
//    IN  PEVENT event,
//    IN uint64_t Milliseconds
//) 
//
///*++
//
//    Routine description : 
//    
//        Sleeps the current thread to wait on the specified event.
//
//    Arguments:
//
//        Pointer to EVENT Object.
//        Amount of milliseconds to wait if event doesnt signal us.
//
//    Return Values:
//
//        MT_SUCCESS on wake, other MTSTATUS codes for failure.
//
//    Notes:
//        
//        This function MUST NOT be called on IRQL higher or equal to DISPATCH_LEVEL, as this function is blocking or uses pageable memory.
//
//--*/
//
//{
//    if (!event) return MT_INVALID_ADDRESS;
//    assert((MeGetCurrentIrql() < DISPATCH_LEVEL));
//
//    IRQL flags;
//    PETHREAD curr = PsGetCurrentThread();
//
//    MsAcquireSpinlock(&event->lock, &flags);
//
//    if (event->signaled) {
//        if (event->type == SynchronizationEvent) event->signaled = false;
//        MsReleaseSpinlock(&event->lock, flags);
//        return MT_SUCCESS;
//    }
//
//    // Zero timeout check
//    if (Milliseconds == 0) {
//        MsReleaseSpinlock(&event->lock, flags);
//        return MT_TIMEOUT;
//    }
//
//    // Setup atomic claim and block state
//    curr->InternalThread.WaitStatus = MT_PENDING;
//    curr->InternalThread.ThreadState = THREAD_BLOCKING;
//    curr->InternalThread.WaitBlock.Object = event;
//    curr->InternalThread.WaitBlock.WaitReason = WaitReasonEvent;
//    // Wakeup time is set in MsInsertTimerQueue if it has any.
//    // But wont WaitReason be overriden?
//
//    // Enqueue into the Event waiting queue
//    MeEnqueueThread(&event->waitingQueue, curr);
//
//    // Enqueue into Timer Queue if a valid timeout is provided
//    if (Milliseconds != MT_INFINITE) {
//        uint64_t Ticks = Milliseconds / TICK_MS;
//        if (Milliseconds % TICK_MS) Ticks++;
//        uint64_t Now = InterlockedLoadAcquire(&MeSystemTickCount);
//        uint64_t WakeupTime = Ticks > UINT64_MAX - Now
//            ? UINT64_MAX
//            : Now + Ticks;
//        MsInsertTimerQueue(&curr->InternalThread, WakeupTime);
//    }
//
//    MsReleaseSpinlock(&event->lock, flags);
//
//#ifdef WAIT_DEBUG
//    gop_printf(COLOR_PURPLE, "Sleeping current thread: %p (owner %s)\n", PsGetCurrentThread(), PsGetCurrentProcess()->ImageName);
//#endif
//
//    // Yield Execution
//    MsYieldExecution(&curr->InternalThread.TrapRegisters);
//
//    // At this point, WaitStatus is either MT_SUCCESS or MT_TIMEOUT
//    MTSTATUS finalStatus = curr->InternalThread.WaitStatus;
//
//    // Unlink from queues to prevent memory corruption
//    if (finalStatus == MT_TIMEOUT) {
//        MsAcquireSpinlock(&event->lock, &flags);
//        MeRemoveThreadFromQueue(&event->waitingQueue, curr);
//        MsReleaseSpinlock(&event->lock, flags);
//    }
//    else if (finalStatus == MT_SUCCESS && Milliseconds != MT_INFINITE) {
//        // Event woke us, so we might still be in the Timer queue.
//        MsRemoveTimerQueue(&curr->InternalThread);
//    }
//
//    curr->InternalThread.WaitBlock.Object = NULL;
//    curr->InternalThread.WaitBlock.WaitReason = WaitReasonNone;
//    curr->InternalThread.WaitBlock.WakeupTime = 0;
//    return finalStatus;
//}
