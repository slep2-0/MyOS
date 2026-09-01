/*++

Module Name:

    priority.c

Purpose:

    This translation unit contains the implementation of thread priorities, including boost.

Author:

    slep (Matanel) 2026.

Revision History:

--*/

#include "../../includes/me.h"
#include "../../includes/ps.h"
#include "../../assert.h"

MTSTATUS
MeSetThreadBasePriority(
    IN PETHREAD Thread,
    IN THREAD_PRIORITY NewPriority,
    OUT THREAD_PRIORITY* PreviousPriority
)

{
    if (!Thread || !PreviousPriority || NewPriority > MT_PRIORITY_REALTIME_HIGHEST) return MT_INVALID_PARAM;

    // ReadyProcessor can be NULL on the target thread when transitioning to THREAD_XXXX to THREAD_READY
    // so even though the thread is READY the ready processor can still not be set in memory, so we just retry if thats the case.

    while (true) {
        PPROCESSOR Processor = InterlockedLoadAcquire(&Thread->InternalThread.ReadyProcessor);

        // A non-NULL ReadyProcessor suggests that the thread is READY and tells us which queue to lock.
        // It is only a snapshot, so validate the state and queue ownership after both locks are held.
        if (Processor) {
            IRQL processorReadyLock;

            // Acquire both locks in order.
            MsAcquireSpinlock(&Processor->readyQueue.Lock, &processorReadyLock);
            MsAcquireSpinlockAtDpcLevel(&Thread->InternalThread.SchedulerLock);

            // Verify that we have the same processor AND that it is ready
            // and that we are in the ready queue
            if (InterlockedLoadAcquire(&Thread->InternalThread.ReadyProcessor) != Processor ||
                InterlockedLoadAcquire(&Thread->InternalThread.ThreadState) != THREAD_READY ||
                !ListContains(&Processor->readyQueue.ListHead, &Thread->SchedulerListEntry))
            {
                // oooh very badd ohhhhh
                MsReleaseSpinlockFromDpcLevel(&Thread->InternalThread.SchedulerLock);
                MsReleaseSpinlock(&Processor->readyQueue.Lock, processorReadyLock);

                // Re-run with new state applied to the thread
                continue;
            }

            // Set both base and normal dynamic priority
            // on boosts the dynamic priority changes
            *PreviousPriority = Thread->InternalThread.BasePriority;

            // Save old EFFECTIVE priority so we know whether to re-order the list on change.
            THREAD_PRIORITY OldEffectivePriority = Thread->InternalThread.Priority;
            Thread->InternalThread.Priority = NewPriority;
            Thread->InternalThread.BasePriority = NewPriority;

            // Move the thread in the queue if the old priority isnt the same as the new one
            if (OldEffectivePriority != NewPriority) {
                RemoveEntryList(&Thread->SchedulerListEntry);

                // The thread is temporarily in no ready queue, so publish a NULL ready processor.
                // MeEnqueueThread republishes the processor after linking it again.
                InterlockedStoreRelease(&Thread->InternalThread.ReadyProcessor, NULL);

                // Re-insert while both locks are still held.
                MeEnqueueThread(&Processor->readyQueue, Thread);
            }

            MsReleaseSpinlockFromDpcLevel(&Thread->InternalThread.SchedulerLock);
            MsReleaseSpinlock(&Processor->readyQueue.Lock, processorReadyLock);

            // The queue was re-ordered, so ask this processor to compare its current thread
            // against the new highest-priority ready thread.
            if (OldEffectivePriority != NewPriority) {
                MeRequestPreemption(Processor);
            }

            return MT_SUCCESS;
        }

        // Acquire thread's lock now since we dont have its processor (running or blocking)
        IRQL prevIrql;
        MsAcquireSpinlock(&Thread->InternalThread.SchedulerLock, &prevIrql);

        // Reload after taking SchedulerLock because ready ownership may have changed while we waited.
        // SchedulerLock serializes scheduling attributes; state and queue ownership are still validated separately.
        Processor = InterlockedLoadAcquire(&Thread->InternalThread.ReadyProcessor);
        THREAD_STATE State = InterlockedLoadAcquire(&Thread->InternalThread.ThreadState);

        // If the refreshed processor is non null, retry the loop
        if (Processor != NULL) {
            // Queue ownership is currently changing. Retry.
            MsReleaseSpinlock(&Thread->InternalThread.SchedulerLock, prevIrql);
            continue;
        }

        if (State == THREAD_RUNNING) {
            // Thread is running right now, request remote processor evaluation
            // Validate that the state actually holds valid data
            // meaning, the thread must have an active processor, and its ready processor must be NULL
            PPROCESSOR ActiveProcessor = InterlockedLoadAcquire(&Thread->InternalThread.ActiveProcessor);
            if (ActiveProcessor == NULL ||
                InterlockedLoadAcquire(&Thread->InternalThread.ReadyProcessor) != NULL)
            {
                // Transitioning state
                MsReleaseSpinlock(&Thread->InternalThread.SchedulerLock, prevIrql);
                continue;
            }

            *PreviousPriority = Thread->InternalThread.BasePriority;
            THREAD_PRIORITY OldEffectivePriority = Thread->InternalThread.Priority;
            Thread->InternalThread.BasePriority = NewPriority;
            Thread->InternalThread.Priority = NewPriority;
            MsReleaseSpinlock(&Thread->InternalThread.SchedulerLock, prevIrql);

            // Ask the saved active CPU to reevaluate after the effective priority changes.
            // If the thread switched away meanwhile, its next enqueue already observes the new priority.
            if (OldEffectivePriority != NewPriority) {
                MeRequestPreemption(ActiveProcessor);
            }

            return MT_SUCCESS;
        }
        else if (State == THREAD_BLOCKED || State == THREAD_BLOCKING || State == THREAD_INITIALIZED) {
            // Change the priority. The next enqueue will use the new value.
            THREAD_PRIORITY OldPriority = Thread->InternalThread.BasePriority;
            Thread->InternalThread.Priority = NewPriority;
            Thread->InternalThread.BasePriority = NewPriority;
            *PreviousPriority = OldPriority;
            MsReleaseSpinlock(&Thread->InternalThread.SchedulerLock, prevIrql);
            return MT_SUCCESS;
        }
        else if (State == THREAD_READY) {
            // Queue ownership is currently changing. Retry.
            MsReleaseSpinlock(&Thread->InternalThread.SchedulerLock, prevIrql);
            continue;
        }
        else {
            // Terminating or terminated.
            MsReleaseSpinlock(&Thread->InternalThread.SchedulerLock, prevIrql);
            return MT_THREAD_IS_TERMINATING;
        }
    }
}

void
MeBoostThread(
    IN PITHREAD Thread,
    IN THREAD_PRIORITY PriorityIncrement
)

{
    if (!Thread || !PriorityIncrement || PriorityIncrement > MT_PRIORITY_REALTIME_HIGHEST) return;

    // Handle thread's priority queue rotation the same way we handle it in the set function
    while (true) {
        PPROCESSOR Processor = InterlockedLoadAcquire(&Thread->ReadyProcessor);

        // A non-NULL ReadyProcessor suggests that the thread is READY and tells us which queue to lock.
        // It is only a snapshot, so validate the state and queue ownership after both locks are held.
        if (Processor) {
            IRQL processorReadyLock;

            // Acquire both locks in order.
            MsAcquireSpinlock(&Processor->readyQueue.Lock, &processorReadyLock);
            MsAcquireSpinlockAtDpcLevel(&Thread->SchedulerLock);

            if (Thread->BasePriority >= MT_PRIORITY_REALTIME_LOWEST) {
                // No need to modify priority when base is a non variable one.
                MsReleaseSpinlockFromDpcLevel(&Thread->SchedulerLock);
                MsReleaseSpinlock(
                    &Processor->readyQueue.Lock,
                    processorReadyLock
                );
                return;
            }

            THREAD_PRIORITY CurrentPriority = Thread->Priority;
            THREAD_PRIORITY NewPriority = MT_PRIORITY_IDLE;

            // Avoid overflow and clamp priority to highest.
            if (WILL_ADD_OVERFLOW(CurrentPriority, PriorityIncrement) || CurrentPriority + PriorityIncrement > MT_PRIORITY_HIGHEST_VARIABLE) {
                NewPriority = MT_PRIORITY_HIGHEST_VARIABLE;
            }
            else {
                // Just normal addition
                NewPriority = CurrentPriority + PriorityIncrement;
            }

            // Verify that we have the same processor AND that it is ready
            // and that we are in the ready queue
            if (InterlockedLoadAcquire(&Thread->ReadyProcessor) != Processor ||
                InterlockedLoadAcquire(&Thread->ThreadState) != THREAD_READY ||
                !ListContains(&Processor->readyQueue.ListHead, &PsGetEThreadFromIThread(Thread)->SchedulerListEntry))
            {
                // oooh very badd ohhhhh
                MsReleaseSpinlockFromDpcLevel(&Thread->SchedulerLock);
                MsReleaseSpinlock(&Processor->readyQueue.Lock, processorReadyLock);

                // Re-run with new state applied to the thread
                continue;
            }

            // Save old EFFECTIVE priority so we know whether to re-order the list on change.
            THREAD_PRIORITY OldEffectivePriority = Thread->Priority;
            Thread->Priority = NewPriority;

            // Move the thread in the queue if the old priority isnt the same as the new one
            if (OldEffectivePriority != NewPriority) {
                RemoveEntryList(&PsGetEThreadFromIThread(Thread)->SchedulerListEntry);

                // The thread is temporarily in no ready queue, so publish a NULL ready processor.
                // MeEnqueueThread republishes the processor after linking it again.
                InterlockedStoreRelease(&Thread->ReadyProcessor, NULL);

                // Re-insert while both locks are still held.
                MeEnqueueThread(&Processor->readyQueue, PsGetEThreadFromIThread(Thread));
            }

            MsReleaseSpinlockFromDpcLevel(&Thread->SchedulerLock);
            MsReleaseSpinlock(&Processor->readyQueue.Lock, processorReadyLock);

            // The queue was re-ordered, so ask this processor to compare its current thread
            // against the new highest-priority ready thread.
            if (OldEffectivePriority != NewPriority) {
                MeRequestPreemption(Processor);
            }

            return;
        }

        // Acquire thread's lock now since we dont have its processor (running or blocking)
        IRQL prevIrql;
        MsAcquireSpinlock(&Thread->SchedulerLock, &prevIrql);

        if (Thread->BasePriority >= MT_PRIORITY_REALTIME_LOWEST) {
            // No need to modify priority when base is a non variable one.
            MsReleaseSpinlock(&Thread->SchedulerLock, prevIrql);
            return;
        }

        // Reload after taking SchedulerLock because ready ownership may have changed while we waited.
        // SchedulerLock serializes scheduling attributes; state and queue ownership are still validated separately.
        Processor = InterlockedLoadAcquire(&Thread->ReadyProcessor);
        THREAD_STATE State = InterlockedLoadAcquire(&Thread->ThreadState);

        // If the refreshed processor is non null, retry the loop
        if (Processor != NULL) {
            // Queue ownership is currently changing. Retry.
            MsReleaseSpinlock(&Thread->SchedulerLock, prevIrql);
            continue;
        }

        THREAD_PRIORITY CurrentPriority = Thread->Priority;
        THREAD_PRIORITY NewPriority = MT_PRIORITY_IDLE;

        // Avoid overflow and clamp priority to highest.
        if (WILL_ADD_OVERFLOW(CurrentPriority, PriorityIncrement) || CurrentPriority + PriorityIncrement > MT_PRIORITY_HIGHEST_VARIABLE) {
            NewPriority = MT_PRIORITY_HIGHEST_VARIABLE;
        }
        else {
            // Just normal addition
            NewPriority = CurrentPriority + PriorityIncrement;
        }

        if (State == THREAD_RUNNING) {
            // Thread is running right now, request remote processor evaluation
            // Validate that the state actually holds valid data
            // meaning, the thread must have an active processor, and its ready processor must be NULL
            PPROCESSOR ActiveProcessor = InterlockedLoadAcquire(&Thread->ActiveProcessor);
            if (ActiveProcessor == NULL ||
                InterlockedLoadAcquire(&Thread->ReadyProcessor) != NULL)
            {
                // Transitioning state
                MsReleaseSpinlock(&Thread->SchedulerLock, prevIrql);
                continue;
            }

            THREAD_PRIORITY OldEffectivePriority = Thread->Priority;
            Thread->Priority = NewPriority;
            MsReleaseSpinlock(&Thread->SchedulerLock, prevIrql);

            // Ask the saved active CPU to reevaluate after the effective priority changes.
            // If the thread switched away meanwhile, its next enqueue already observes the new priority.
            if (OldEffectivePriority != NewPriority) {
                MeRequestPreemption(ActiveProcessor);
            }

            return;
        }
        else if (State == THREAD_BLOCKED || State == THREAD_BLOCKING || State == THREAD_INITIALIZED) {
            // Change the priority. The next enqueue will use the new value.
            Thread->Priority = NewPriority;
            MsReleaseSpinlock(&Thread->SchedulerLock, prevIrql);
            return;
        }
        else if (State == THREAD_READY) {
            // Queue ownership is currently changing. Retry.
            MsReleaseSpinlock(&Thread->SchedulerLock, prevIrql);
            continue;
        }
        else {
            // Terminating or terminated.
            MsReleaseSpinlock(&Thread->SchedulerLock, prevIrql);
            return;
        }
    }
}

void
MeDecayThreadPriority(
    IN PITHREAD Thread
)

{
    // A thread must be running for the decay to work
    // per contract with MiHandleTimer
    assert(InterlockedLoadAcquire(&Thread->ThreadState) == THREAD_RUNNING);

    // Lock the thread's scheduling attributes.
    // This function is called from MiHandleTimer so IRQL is though the roof
    // aka its CLOCK_LEVEL, so we must use the non raise DPC spinlock
    MsAcquireSpinlockAtDpcLevel(&Thread->SchedulerLock);

    // If the base priority is less than realtime AND the priority is above its base (i.e the thread is boosted)
    // then decrement the boosted priority
    if (Thread->BasePriority < MT_PRIORITY_REALTIME_LOWEST && Thread->Priority > Thread->BasePriority) {
        Thread->Priority--;
    }

    // stuff here
    MsReleaseSpinlockFromDpcLevel(&Thread->SchedulerLock);
    return;
}
