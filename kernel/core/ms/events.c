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

/*++

    Routine description:

        Removes the first wait block from a dispatcher wait list while the
        caller holds the dispatcher lock.

    Arguments:

        [IN OUT] HeaderWaitListHead - The protected dispatcher wait list.

    Return Values:

        The waiting thread represented by the removed wait block, or NULL when
        the list is empty.

--*/

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

    Routine description:

        Initializes an EVENT object to the specified values.

    Arguments:

        [OUT] Event - Resident event storage to initialize.
        [IN] EventDispatcherType - Synchronization or notification event type.
        [IN] StartSignaled - Whether the event starts signaled.

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

    Routine description:
    
        Sets an event to wake threads waiting on it.

    Arguments:
    
        [IN OUT] event - The resident event object to signal.
        [OUT OPTIONAL] PreviousState - Receives the prior signal state.

    Return Values:

        MT_SUCCESS on success, or an error status when event is invalid.

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

/*++

    Routine description:

        Signals an event without requesting its previous signal state.

    Arguments:

        [IN OUT] Event - The event object to signal.

    Return Values:

        MT_SUCCESS on success, or an error status when Event is invalid.

--*/
{
    return MsSetEventEx(Event, NULL);
}

bool
MsResetEvent(
    IN PEVENT Event
)

/*++

    Routine description:

        Resets an event back to its non-signaled state.

    Arguments:

        [IN OUT] Event - The event object to reset.

    Return Values:

        The previous Boolean signal state.

--*/

{
    IRQL prevIrql;
    MsAcquireSpinlock(&Event->Header.Lock, &prevIrql);
    bool PreviousState = Event->Header.SignalState != 0;
    Event->Header.SignalState = 0;
    MsReleaseSpinlock(&Event->Header.Lock, prevIrql);
    return PreviousState;
}
