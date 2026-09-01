/*++

Module Name:

    balancing.c

Purpose:

    This translation unit contains scheduler load-balancing operations.

Author:

    slep (Matanel) 2026.

Revision History:

--*/

#include "../../includes/me.h"
#include "../../includes/ps.h"
#include "../../assert.h"

static
inline
bool
MepReadyQueueContainsThreadLocked(
    IN PREADY_QUEUE ReadyQueue,
    IN PETHREAD Thread
)

/*++

    Routine description:

        Reports whether a thread is present in a ready queue while its lock is held.

    Arguments:

        [IN] ReadyQueue - Ready queue examined while its lock is held.
        [IN] Thread - Thread affected by the operation.

    Return Values:

        A nonzero value when the reported condition holds, or zero otherwise.

--*/

{
    PDOUBLY_LINKED_LIST ListHead = &ReadyQueue->ListHead;
    for (PDOUBLY_LINKED_LIST Entry = ListHead->Flink;
         Entry != ListHead;
         Entry = Entry->Flink) {
        if (Entry == &Thread->SchedulerListEntry) return true;
    }

    return false;
}

bool
MepMigrateReadyThread(
    PETHREAD Thread,
    PPROCESSOR Source,
    PPROCESSOR Destination
)

/*++

    Routine description:

        Migrates a ready thread from one processor queue to another.

    Arguments:

        Thread - The thread to migrate to Destination from Source.
        Source - The processor whose ready queue currently owns the thread.
        Destination - The processor whose ready queue will receive the thread.

    Return Values:

        True if the thread was moved, false otherwise.

--*/

{
    if (!Thread || !Source || !Destination) return false;

    // The source CPU must not be the destination CPU.
    if (Source == Destination) return false;

    bool Moved = false;
    IRQL PreviousIrql;
    MepAcquireOrderedReadyQueueLocks(
        Source,
        Destination,
        &PreviousIrql
    );

    // The thread scheduler lock follows both ready-queue locks.
    MsAcquireSpinlockAtDpcLevel(&Thread->InternalThread.SchedulerLock);

    if (!MeIsProcessorAllowed(
            &Thread->InternalThread,
            Destination
        )) {
        goto Cleanup;
    }

    if (!MepReadyQueueContainsThreadLocked(
            &Source->readyQueue,
            Thread
        )) {
        goto Cleanup;
    }

    if (InterlockedLoadAcquire(
            &Thread->InternalThread.ThreadState
        ) != THREAD_READY) {
        goto Cleanup;
    }

    if (InterlockedLoadAcquire(
            &Thread->InternalThread.ActiveProcessor
        ) != NULL) {
        goto Cleanup;
    }

    if (InterlockedLoadAcquire(
            &Thread->InternalThread.ReadyProcessor
        ) != Source) {
        goto Cleanup;
    }

    if (!MeRemoveThreadFromQueue(&Source->readyQueue, Thread)) {
        assert(
            false,
            "Ready queue corruption detected while migrating a validated thread"
        );
        goto Cleanup;
    }

    InterlockedStoreRelease(
        &Thread->InternalThread.ReadyProcessor,
        NULL
    );
    MeEnqueueThread(&Destination->readyQueue, Thread);
    Moved = true;

Cleanup:
    MsReleaseSpinlockFromDpcLevel(
        &Thread->InternalThread.SchedulerLock
    );
    MepReleaseOrderedReadyQueueLocks(
        Source,
        Destination,
        PreviousIrql
    );

    if (Moved) {
        MeRequestPreemption(Destination);
    }

    return Moved;
}

PPROCESSOR
MepSelectLeastLoadedAllowedProcessor(
    IN PITHREAD Thread,
    IN PPROCESSOR PreferredProcessor // selected only at a tie of Preferred and Lowest.
)

{
    if (!Thread || !PreferredProcessor) return NULL;

    // Acquire thread lock to see affinity mask
    uint32_t AffinityMask = 0;

    IRQL oldIrql;
    MsAcquireSpinlock(&Thread->SchedulerLock, &oldIrql);
    AffinityMask = Thread->AllowedProcessorMask;
    MsReleaseSpinlock(&Thread->SchedulerLock, oldIrql);

    PPROCESSOR SmallestCountProcessor = NULL;
    uint32_t SmallestCount = UINT32_MAX;

    // Scan all processors
    for (uint8_t i = 0; i < MeGetActiveProcessorCount(); i++) {
        PPROCESSOR Processor = MeGetProcessorBlock(i);
        
        // Ignore offline or affinity excluded cpus
        if (InterlockedLoadAcquire(&Processor->State) != ProcessorStateOnline) continue;
        
        if ((AffinityMask & (1u << Processor->ID)) == 0) {
            continue;
        }

        // Acquire this processor ready queue lock
        MsAcquireSpinlock(&Processor->readyQueue.Lock, &oldIrql);

        uint32_t ReadyThreads = Processor->readyQueue.ThreadCount;

        if (ReadyThreads < SmallestCount || (ReadyThreads == SmallestCount && Processor == PreferredProcessor)) {
            // If this processor has the smallest threads ready, then set new processor and limit.
            // OR, if it has the same count as smallest count and this is the preferred processor
            // just set to preferred.
            SmallestCount = ReadyThreads;
            SmallestCountProcessor = Processor;
        }

        MsReleaseSpinlock(&Processor->readyQueue.Lock, oldIrql);
    }

    // Return the chosen processor with the least ready threads (or preferred one if its equal to least)
    return SmallestCountProcessor;
}