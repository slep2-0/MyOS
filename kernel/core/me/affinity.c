/*++

Module Name:

    affinity.c

Purpose:

    This translation unit contains the implementation of thread affinities.

Author:

    slep (Matanel) 2026.

Revision History:

--*/

#include "../../includes/me.h"
#include "../../includes/ps.h"
#include "../../includes/mh.h"
#include "../../assert.h"

extern PROCESSOR cpus[];

MTSTATUS
MeSetThreadAffinityMask(
    PITHREAD Thread,
    uint32_t NewMask,
    uint32_t* PreviousMask
)

{
    if (!Thread || !NewMask || !PreviousMask) return MT_INVALID_PARAM;

    // NewMask must contain active processor bits
    uint32_t ValidMask = MeGetActiveProcessorMask();
    
    if ((NewMask & ~ValidMask) != 0) {
        // Caller gave invalid mask
        return MT_INVALID_PARAM;
    }

    while (true) {
        PPROCESSOR Processor = InterlockedLoadAcquire(&Thread->ReadyProcessor);

        // A non-NULL ReadyProcessor suggests that the thread is READY and tells us which queue to lock.
        // It is only a snapshot, so validate the state and queue ownership after both locks are held.
        if (Processor) {
            PPROCESSOR SourceProcessor = Processor;
            PETHREAD EThread = PsGetEThreadFromIThread(Thread);

            // If the source remains allowed, only the source queue and thread
            // need to be locked while the mask is replaced.
            if ((NewMask & (1u << SourceProcessor->ID)) != 0) {
                IRQL SourceIrql;
                MsAcquireSpinlock(
                    &SourceProcessor->readyQueue.Lock,
                    &SourceIrql
                );
                MsAcquireSpinlockAtDpcLevel(&Thread->SchedulerLock);

                if (InterlockedLoadAcquire(&Thread->ReadyProcessor) != SourceProcessor ||
                    InterlockedLoadAcquire(&Thread->ThreadState) != THREAD_READY ||
                    InterlockedLoadAcquire(&Thread->ActiveProcessor) != NULL ||
                    !ListContains(
                        &SourceProcessor->readyQueue.ListHead,
                        &EThread->SchedulerListEntry
                    ))
                {
                    MsReleaseSpinlockFromDpcLevel(&Thread->SchedulerLock);
                    MsReleaseSpinlock(
                        &SourceProcessor->readyQueue.Lock,
                        SourceIrql
                    );
                    continue;
                }

                *PreviousMask = Thread->AllowedProcessorMask;
                Thread->AllowedProcessorMask = NewMask;

                MsReleaseSpinlockFromDpcLevel(&Thread->SchedulerLock);
                MsReleaseSpinlock(
                    &SourceProcessor->readyQueue.Lock,
                    SourceIrql
                );
                return MT_SUCCESS;
            }

            // The source is excluded by the new mask. Select a permitted online
            // destination before acquiring the two ready-queue locks.
            PPROCESSOR DestinationProcessor = NULL;
            for (uint8_t Index = 0;
                 Index < MeGetActiveProcessorCount();
                 Index++) {
                PPROCESSOR Candidate = &cpus[Index];
                if ((NewMask & (1u << Candidate->ID)) != 0 &&
                    InterlockedLoadAcquire(&Candidate->State) == ProcessorStateOnline)
                {
                    DestinationProcessor = Candidate;
                    break;
                }
            }

            if (!DestinationProcessor) {
                return MT_NOT_FOUND;
            }

            // Ready-queue locks are globally ordered by processor ID. The
            // thread SchedulerLock is always acquired after both queue locks.
            bool AcquireSourceFirst =
                SourceProcessor->ID < DestinationProcessor->ID;
            IRQL FirstQueueIrql;

            if (AcquireSourceFirst) {
                MsAcquireSpinlock(
                    &SourceProcessor->readyQueue.Lock,
                    &FirstQueueIrql
                );
                MsAcquireSpinlockAtDpcLevel(
                    &DestinationProcessor->readyQueue.Lock
                );
            }
            else {
                MsAcquireSpinlock(
                    &DestinationProcessor->readyQueue.Lock,
                    &FirstQueueIrql
                );
                MsAcquireSpinlockAtDpcLevel(
                    &SourceProcessor->readyQueue.Lock
                );
            }

            MsAcquireSpinlockAtDpcLevel(&Thread->SchedulerLock);

            bool ValidTransaction =
                InterlockedLoadAcquire(&Thread->ReadyProcessor) == SourceProcessor &&
                InterlockedLoadAcquire(&Thread->ThreadState) == THREAD_READY &&
                InterlockedLoadAcquire(&Thread->ActiveProcessor) == NULL &&
                InterlockedLoadAcquire(&DestinationProcessor->State) == ProcessorStateOnline &&
                (NewMask & (1u << DestinationProcessor->ID)) != 0 &&
                ListContains(
                    &SourceProcessor->readyQueue.ListHead,
                    &EThread->SchedulerListEntry
                );

            if (ValidTransaction) {
                bool Removed = MeRemoveThreadFromQueue(
                    &SourceProcessor->readyQueue.ListHead,
                    EThread
                );
                assert(Removed, "Validated READY thread disappeared from its source queue");

                if (Removed) {
                    *PreviousMask = Thread->AllowedProcessorMask;
                    Thread->AllowedProcessorMask = NewMask;
                    InterlockedStoreRelease(&Thread->ReadyProcessor, NULL);
                    MeEnqueueThread(
                        &DestinationProcessor->readyQueue,
                        EThread
                    );
                }
            }

            MsReleaseSpinlockFromDpcLevel(&Thread->SchedulerLock);

            if (AcquireSourceFirst) {
                MsReleaseSpinlockFromDpcLevel(
                    &DestinationProcessor->readyQueue.Lock
                );
                MsReleaseSpinlock(
                    &SourceProcessor->readyQueue.Lock,
                    FirstQueueIrql
                );
            }
            else {
                MsReleaseSpinlockFromDpcLevel(
                    &SourceProcessor->readyQueue.Lock
                );
                MsReleaseSpinlock(
                    &DestinationProcessor->readyQueue.Lock,
                    FirstQueueIrql
                );
            }

            if (!ValidTransaction) {
                continue;
            }

            MeRequestPreemption(DestinationProcessor);
            return MT_SUCCESS;
        }

        // Acquire thread's lock now since we dont have its processor (running or blocking)
        IRQL prevIrql;
        MsAcquireSpinlock(&Thread->SchedulerLock, &prevIrql);

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

        if (State == THREAD_INITIALIZED) {
            *PreviousMask = Thread->AllowedProcessorMask;
            Thread->AllowedProcessorMask = NewMask;
            MsReleaseSpinlock(&Thread->SchedulerLock, prevIrql);
            return MT_SUCCESS;
        }

        if (State == THREAD_BLOCKED) {

            // Ongoing change
            if (InterlockedLoadAcquire(&Thread->ActiveProcessor) != NULL) {
                MsReleaseSpinlock(&Thread->SchedulerLock, prevIrql);
                continue;
            }

            *PreviousMask = Thread->AllowedProcessorMask;
            Thread->AllowedProcessorMask = NewMask;

            MsReleaseSpinlock(&Thread->SchedulerLock, prevIrql);
            return MT_SUCCESS;
        }

        if (State == THREAD_RUNNING) {
            PPROCESSOR RemoteProcessor = InterlockedLoadAcquire(&Thread->ActiveProcessor);

            // Ongoing change
            if (RemoteProcessor == NULL) {
                MsReleaseSpinlock(&Thread->SchedulerLock, prevIrql);
                continue;
            }

            *PreviousMask = Thread->AllowedProcessorMask;
            Thread->AllowedProcessorMask = NewMask;

            bool RemoteProcessorAllowed = true;

            if (!MeIsProcessorAllowed(Thread, RemoteProcessor)) {
                // Remote running processor on the thread is no longer allowed to own it
                // request remote preemption, the remote processor will realize this thread is not allowed to run, and will switch
                RemoteProcessorAllowed = false;
            }

            MsReleaseSpinlock(&Thread->SchedulerLock, prevIrql);

            if (!RemoteProcessorAllowed) {
                MeRequestPreemption(RemoteProcessor);
            }

            return MT_SUCCESS;
        }
        else if (State == THREAD_BLOCKING || State == THREAD_READY) {
            // Queue ownership is currently changing. Retry.
            MsReleaseSpinlock(&Thread->SchedulerLock, prevIrql);
            continue;
        }
        else {
            // Terminating or terminated.
            MsReleaseSpinlock(&Thread->SchedulerLock, prevIrql);
            return MT_THREAD_IS_TERMINATING;
        }
    }
}
