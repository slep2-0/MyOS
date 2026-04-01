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
            if (__sync_val_compare_and_swap(&waiter->InternalThread.WaitStatus, MT_PENDING, MT_SUCCESS) == MT_PENDING) {
                event->signaled = false;
                MsReleaseSpinlock(&event->lock, flags);

                waiter->InternalThread.ThreadState = THREAD_READY;
                MeEnqueueThreadWithLock(&MeGetCurrentProcessor()->readyQueue, waiter);
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
        if (__sync_val_compare_and_swap(&t->InternalThread.WaitStatus, MT_PENDING, MT_SUCCESS) == MT_PENDING) {
            t->InternalThread.ThreadState = THREAD_READY;
            MeEnqueueThreadWithLock(&MeGetCurrentProcessor()->readyQueue, t);
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
    curr->InternalThread.ThreadState = THREAD_BLOCKED;

    // Enqueue into the Event waiting queue
    MeEnqueueThread(&event->waitingQueue, curr);
    MsReleaseSpinlock(&event->lock, flags);

    // Enqueue into Timer Queue if a valid timeout is provided
    if (Milliseconds != INFINITE) {
        uint64_t Ticks = (Milliseconds + TICK_MS - 1) / TICK_MS;

        IRQL tflags;
        MsAcquireSpinlock(&MsTimerQueueLock, &tflags);

        curr->InternalThread.WaitBlock.WakeupTime = MeSystemTickCount + Ticks;
        curr->InternalThread.WaitBlock.WaitReason = Sleeping; // Or a new WaitReason

        // --- Insert SORTED into MeTimerQueue ---
        if (IsListEmpty(&MsTimerQueue)) {
            InsertTailList(&MsTimerQueue, &curr->InternalThread.WaitBlock.WaitBlockList);
        }
        else {
            PDOUBLY_LINKED_LIST CurrentNode = MsTimerQueue.Flink;
            bool Inserted = false;
            while (CurrentNode != &MsTimerQueue) {
                PITHREAD Block = CONTAINING_RECORD(CurrentNode, ITHREAD, WaitBlock.WaitBlockList);
                if (curr->InternalThread.WaitBlock.WakeupTime < Block->WaitBlock.WakeupTime) {
                    curr->InternalThread.WaitBlock.WaitBlockList.Flink = CurrentNode;
                    curr->InternalThread.WaitBlock.WaitBlockList.Blink = CurrentNode->Blink;
                    CurrentNode->Blink->Flink = &curr->InternalThread.WaitBlock.WaitBlockList;
                    CurrentNode->Blink = &curr->InternalThread.WaitBlock.WaitBlockList;
                    Inserted = true;
                    break;
                }
                CurrentNode = CurrentNode->Flink;
            }
            if (!Inserted) InsertTailList(&MsTimerQueue, &curr->InternalThread.WaitBlock.WaitBlockList);
        }
        MsReleaseSpinlock(&MsTimerQueueLock, tflags);
    }

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

        // We need to replace this with QueueEntryRemove function.
        if (curr->SchedulerListEntry.Flink != NULL || curr->SchedulerListEntry.Blink != NULL || event->waitingQueue.head == curr) {

            if (curr->SchedulerListEntry.Blink) {
                curr->SchedulerListEntry.Blink->Flink = curr->SchedulerListEntry.Flink;
            }
            else {
                event->waitingQueue.head = CONTAINING_RECORD(curr->SchedulerListEntry.Flink, ETHREAD, SchedulerListEntry);
            }

            if (curr->SchedulerListEntry.Flink) {
                curr->SchedulerListEntry.Flink->Blink = curr->SchedulerListEntry.Blink;
            }
            else {
                event->waitingQueue.tail = CONTAINING_RECORD(curr->SchedulerListEntry.Blink, ETHREAD, SchedulerListEntry);
            }

            curr->SchedulerListEntry.Flink = NULL;
            curr->SchedulerListEntry.Blink = NULL;
        }

        MsReleaseSpinlock(&event->lock, flags);
    }
    else if (finalStatus == MT_SUCCESS && Milliseconds != INFINITE) {
        // Event woke us, so we might still be in the Timer queue.
        IRQL tflags;
        MsAcquireSpinlock(&MsTimerQueueLock, &tflags);
        if (curr->InternalThread.WaitBlock.WaitBlockList.Flink != NULL) { // If NULL, DPC popped us
            RemoveEntryList(&curr->InternalThread.WaitBlock.WaitBlockList);
            curr->InternalThread.WaitBlock.WaitBlockList.Flink = NULL;
            curr->InternalThread.WaitBlock.WaitBlockList.Blink = NULL;
        }
        MsReleaseSpinlock(&MsTimerQueueLock, tflags);
    }

    return finalStatus;
}
