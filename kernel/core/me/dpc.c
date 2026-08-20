/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      DPC Implementation.
 */

#include "../../includes/me.h"
#include "../../includes/mg.h"
#include "../../includes/ps.h"
#include "../../includes/mh.h"
#include "../../assert.h"
#include "../../includes/ob.h"
#include "../../includes/md.h"

void
MeRequestCurrentDpcInterrupt(
    void
)

/*++

    Routine description:

        Requests DPC retirement on the current processor.

    Arguments:

        None.

    Return Values:

        None.

--*/

{
    bool InterruptsEnabled = MeDisableInterrupts();
    PPROCESSOR Cpu = MeGetCurrentProcessor();

    // Publishing the request and posting the self-IPI must be one local-CPU
    // transaction. Otherwise a timer/DPC interrupt can retire the request in
    // the gap and leave MhRequestSoftwareInterrupt with no request to service.
    InterlockedStoreRelease(
        &Cpu->DpcInterruptRequested,
        true
    );

    if (!InterlockedLoadAcquire(&Cpu->DpcRoutineActive)) {
        MhRequestSoftwareInterrupt(DISPATCH_LEVEL);
    }

    MeEnableInterrupts(InterruptsEnabled);
}

bool
MeInsertQueueDpc(
    IN PDPC Dpc,
    IN void* SystemArgument1,
    IN void* SystemArgument2
)

/*++

    Routine description:

        This function inserts the DPC object into the DPC queue.
        If the DPC object is already in the queue, nothing is performed.
        Else, the DPC Object is inserted in the queue, and a software interrupt is generated based on the DPC priority & current depth.

    Arguments:

        [IN]    PDPC Dpc - The DPC Object to queue.
        [IN]    void* SystemArgument1 - Optional Argument for the DPC to receive.
        [IN]    void* SystemArgument2 - Optional Argument for the DPC to receive.

    Return Values:

        If the DPC object is already in the queue, false is returned.
        Otherwise, true is returned.

    Notes:

        For setting a certain CPU to run this DPC, use the MeSetTargetProcessorDpc function before calling this one.

--*/

{
    // Declarations
    PDPC_DATA DpcData;
    PPROCESSOR Cpu;
    bool Inserted = false;
    bool RequestInterrupt = false;
    PPROCESSOR RequestCpu = NULL;
    IRQL OldIrql;

    if (!Dpc->DeferredRoutine) {
#ifdef DEBUG
        MeBugCheckEx(DPC_NOT_INITIALIZED,
            (void*)Dpc,
            (void*)(uintptr_t)RETADDR(0),
            NULL,
            NULL
        );
#else
        MeBugCheckEx(DPC_NOT_INITIALIZED,
            (void*)Dpc,
            NULL,
            NULL,
            NULL
        );
#endif
    }

    // Disable local interrupt delivery while publishing into a processor's DPC queue.
    MeRaiseIrql(HIGH_LEVEL, &OldIrql);

    if (Dpc->CpuNumber < MeGetActiveProcessorCount() && Dpc->CpuNumber != DPC_TARGET_CURRENT) {
        Cpu = MeGetProcessorBlock(Dpc->CpuNumber);
    }
    else {
        Cpu = MeGetCurrentProcessor();
    }

    DpcData = &Cpu->DpcData;

    // Acquire the selected target processor's DPC queue lock.
    MsAcquireSpinlockAtDpcLevel(&DpcData->DpcLock);

    // Atomic operation to check if this DPC is already queued.
    if (InterlockedCompareExchangePointer(&Dpc->DpcData, DpcData, NULL) == NULL) {

        // Success: It was not queued.
        DpcData->DpcQueueDepth += 1;
        DpcData->DpcCount += 1;
        Dpc->SystemArgument1 = SystemArgument1;
        Dpc->SystemArgument2 = SystemArgument2;

        // Insert Head (High Priority) or Tail (Normal)
        // >= to keep when i'll bring back SYSTEM_PRIORITY, so it wont put them at normal level.
        if (Dpc->priority >= HIGH_PRIORITY) {
            InsertHeadList(&DpcData->DpcListHead, &Dpc->DpcListEntry);
        }
        else {
            InsertTailList(&DpcData->DpcListHead, &Dpc->DpcListEntry);
        }

        Inserted = true;
        // Increment request rate
        Cpu->DpcRequestRate++;

        // Publish one interrupt request when retirement is not already active
        // and another request has not already been coalesced for this CPU.
        if (!InterlockedLoadAcquire(&Cpu->DpcRoutineActive) &&
            !InterlockedLoadAcquire(&Cpu->DpcInterruptRequested)) {

            // Normal/high priority DPCs request prompt retirement. Low-priority
            // DPCs wait until queue depth reaches the configured threshold.
            if ((Dpc->priority != LOW_PRIORITY) ||
                (DpcData->DpcQueueDepth >= Cpu->MaximumDpcQueueDepth)) {

                // Publish the request under the queue lock; issue it after unlock.
                InterlockedStoreRelease(
                    &Cpu->DpcInterruptRequested,
                    true
                );
                RequestInterrupt = true;
                RequestCpu = Cpu;
            }
        }
    }

    // Release the queue lock and restore the caller's IRQL.
    MsReleaseSpinlockFromDpcLevel(&DpcData->DpcLock);
    MeLowerIrql(OldIrql);

    if (RequestInterrupt && RequestCpu != MeGetCurrentProcessor()) {
        IPI_PARAMS IpiParams = { 0 };
        MhSendActionToSpecificCpuAndWait(RequestCpu, CPU_ACTION_REQUEST_DPC, IpiParams);
    }

    return Inserted;
}

bool
MeRemoveQueueDpc(
    IN PDPC Dpc
)

/*++

    Routine description:

        This function removes the Dpc object from the DPC Queue.
        If the DPC object is NOT in the DPC queue, nothing is performed.
        Otherwise, the DPC object is removed from the queue, and its inserted state (DpcData), is NULL (false).

    Arguments:

        [IN]    PDPC Dpc - The DPC Object to remove from queue.

    Return Values:

        If the DPC object is not in the queue, false is returned.
        Otherwise, true is returned.

--*/

{
    PDPC_DATA DpcData;
    bool Removed = false;
    IRQL OldIrql;

    // DpcData's lock is an at-DPC-level lock. Raising first both satisfies
    // that contract and prevents a local DPC interrupt from racing removal.
    MeRaiseIrql(HIGH_LEVEL, &OldIrql);

    DpcData = (PDPC_DATA)Dpc->DpcData;

    if (DpcData != NULL) {
        // Acquire Lock
        MsAcquireSpinlockAtDpcLevel(&DpcData->DpcLock);

        // Check if still queued
        if (DpcData == Dpc->DpcData) {
            assert(DpcData->DpcQueueDepth != 0);
            if (DpcData->DpcQueueDepth != 0) {
                DpcData->DpcQueueDepth -= 1;
            }
            RemoveEntryList(&Dpc->DpcListEntry);
            InterlockedExchangePointer(&Dpc->DpcData, NULL); // Mark as not queued
            Removed = true;
        }

        // Release Lock
        MsReleaseSpinlockFromDpcLevel(&DpcData->DpcLock);
    }

    MeLowerIrql(OldIrql);
    return Removed;
}

void
MeRetireDPCs(
    void
)

/*++

    Routine description:

        This function retires the DPC list for the current processor.

    Arguments:

        None.

    Return Values:

        None.

    Notes:
        
        This function is entered with interrupts disabled ( __cli() ), and exits with interrupts disabled.

--*/

{
    // Few assertions.
    assert(MeGetCurrentIrql() == DISPATCH_LEVEL);
    assert(MeAreInterruptsEnabled() == false);

    // Declarations
    PDPC Dpc;
    PDPC_DATA DpcData;
    PDOUBLY_LINKED_LIST Entry;
    PDEFERRED_ROUTINE DeferredRoutine;
    void* DeferredContext;
    void* SystemArgument1;
    void* SystemArgument2;
    PPROCESSOR Cpu = MeGetCurrentProcessor();

    DpcData = &Cpu->DpcData;

    InterlockedStoreRelease(&Cpu->DpcRoutineActive, true);

    for (;;) {
        MsAcquireSpinlockAtDpcLevel(&DpcData->DpcLock);

        Entry = DpcData->DpcListHead.Flink;
        if (Entry == &DpcData->DpcListHead) {
            InterlockedStoreRelease(
                &Cpu->DpcRoutineActive,
                false
            );
            InterlockedStoreRelease(
                &Cpu->DpcInterruptRequested,
                false
            );
            MsReleaseSpinlockFromDpcLevel(&DpcData->DpcLock);
            break;
        }

        RemoveEntryList(Entry);
        Dpc = CONTAINING_RECORD(Entry, DPC, DpcListEntry);

        DeferredRoutine = Dpc->DeferredRoutine;
        DeferredContext = Dpc->DeferredContext;
        SystemArgument1 = Dpc->SystemArgument1;
        SystemArgument2 = Dpc->SystemArgument2;

        // Clear DpcData while the entry is unlinked so the routine may requeue itself.
        InterlockedExchangePointer(&Dpc->DpcData, NULL);
        assert(DpcData->DpcQueueDepth != 0);
        if (DpcData->DpcQueueDepth != 0) {
            DpcData->DpcQueueDepth -= 1;
        }

        MsReleaseSpinlockFromDpcLevel(&DpcData->DpcLock);

        // Enable Interrupts for execution
        __sti();

        Cpu->CurrentDeferredRoutine = Dpc;
#ifdef DEBUG
        if (!DeferredRoutine) {
            // NULL DPC routine.
            MeBugCheckEx(
                DPC_EXECUTE_FAILURE,
                (void*)(uintptr_t)Dpc,
                NULL,
                NULL,
                NULL
            );
        }
#endif

        DeferredRoutine(Dpc, DeferredContext, SystemArgument1, SystemArgument2);
        Cpu->CurrentDeferredRoutine = NULL;

        // Assertion, incase the DPC changed the IRQL level without lowering back to DISPATCH.
        assert(MeGetCurrentIrql() == DISPATCH_LEVEL);

        // Disable Interrupts for next loop iteration
        __cli();
    }

    // Return statement, assert that interrupts are disabled.
    assert(MeAreInterruptsEnabled() == false, "Interrupts must not enabled at DPC Retirement exit");
}

void
MeSetTargetProcessorDpc(
    IN PDPC Dpc,
    IN uint32_t CpuNumber
)

/*++

    Routine description:

        This function ensures that the DPC executes only on the CPU
        corresponding to the supplied LAPIC ID.

    Arguments:

        [IN] PDPC DpcAllocated - Pointer to DPC allocated in resident memory (e.g, pool alloc)
        [IN] uint32_t CpuNumber - Processor-array index on which the DPC should run.

    Return Values:

        None.

    Notes:

        This function call must be made before MeInsertQueueDpc.

--*/

{
    assert(CpuNumber < MeGetActiveProcessorCount());

    Dpc->CpuNumber = CpuNumber;
}

void
MeInitializeDpc(
    IN PDPC DpcAllocated,
    IN PDEFERRED_ROUTINE DeferredRoutine,
    IN void* DeferredContext,
    IN DPC_PRIORITY DeferredPriority
)

/*++

    Routine description:

        This function initializes a DPC to be used for queueing.

    Arguments:

        [IN] PDPC DpcAllocated - Pointer to DPC allocated in resident memory (e.g, pool alloc)
        [IN] PDEFERRED_ROUTINE DeferredRoutine - Pointer to deferred routine for the DPC to execute.
        [IN] void* DeferredContext - Opaque pointer to deferred context, passed to the DeferredRoutine function as a parameter.
        [IN] DPC_PRIORITY DeferredPriority - Supplies the priority of the DPC. A DPC of LOW_PRIORITY will not be executed at queue time unless the depth is full, or a software interrupt occurs.

    Return Values:

        None.

--*/

{
    // Initialize standard DPC headers.
    DpcAllocated->priority = DeferredPriority;
    
    // Initialize address of routine and context param.
    DpcAllocated->DeferredRoutine = DeferredRoutine;
    DpcAllocated->DeferredContext = DeferredContext;
    DpcAllocated->DpcData = NULL;
    
    // Set to current CPU. (the driver can modify his CPU)
    DpcAllocated->CpuNumber = DPC_TARGET_CURRENT;

    // Initialize list head for DPC.
    InitializeListHead(&DpcAllocated->DpcListEntry);
}
