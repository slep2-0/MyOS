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

//Statically made DPC Routines.

extern volatile void* ObpReaperList;

void ReapOb(DPC* dpc, void* DeferredContext, void* SystemArgument1, void* SystemArgument2) {
    /*
    DeferredContext - Ignored
    SystemArgument1 - Ignored
    SystemArgument2 - Ignored
    */

    POBJECT_HEADER head, cur;

    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
    UNREFERENCED_PARAMETER(dpc); // Switched to global, freeing this would cause in MEMORY_CORRUPT_HEADER.

    // Atomically take the list
    head = (POBJECT_HEADER)InterlockedExchangePointer(&ObpReaperList, NULL);

    // Walk the captured chain and free each header
    while (head) {
        cur = head;
        head = (POBJECT_HEADER)head->NextToFree;
        MmFreePool(cur); // Free the header (frees object as well, header + sizeof(header) = object)
    }

}

//End

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

        If the DPC objeect is already in the queue, false is returned.
        Otherwise, true is returned.

--*/

{
    // Declarations
    PDPC_DATA DpcData;
    PPROCESSOR Cpu;
    bool Inserted = false;
    IRQL OldIrql;

    if (!Dpc->DeferredRoutine) {
#ifdef DEBUG
        MeBugCheckEx(DPC_NOT_INITIALIZED,
            (void*)(uintptr_t)RETADDR(0),
            (void*)Dpc,
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

    // Raise IRQL to HIGH_LEVEL to prevent all interrupts while we touch the processor DPC queue. (prevent corruption)
    MeRaiseIrql(HIGH_LEVEL, &OldIrql);

    if (Dpc->CpuNumber < MeGetActiveProcessorCount()) {
        Cpu = MeGetProcessorBlock(Dpc->CpuNumber);
    }
    else {
        Cpu = MeGetCurrentProcessor();
    }

    DpcData = &Cpu->DpcData;

    // Acquire the DpcData lock for the current processor.
    MsAcquireSpinlockAtDpcLevel(&DpcData->DpcLock);

    // Atomic operation to check if this DPC is already queued.
    if (InterlockedCompareExchangePointer(&Dpc->DpcData, DpcData, NULL) == NULL) {

        // Success: It was not queued.
        DpcData->DpcQueueDepth += 1;
        DpcData->DpcCount += 1;
        Dpc->SystemArgument1 = SystemArgument1;
        Dpc->SystemArgument2 = SystemArgument2;

        // Insert Head (High Priority) or Tail (Normal)
        if (Dpc->priority == HIGH_PRIORITY) {
            InsertHeadList(&DpcData->DpcListHead, &Dpc->DpcListEntry);
        }
        else {
            InsertTailList(&DpcData->DpcListHead, &Dpc->DpcListEntry);
        }

        Inserted = true;
        // Increment request rate
        Cpu->DpcRequestRate++;

        // Check if we need to request an interurpt
        // We only request if a DPC isnt currently running.
        // And we haven't already requested an interrupt for a DPC.
        if ((Cpu->DpcRoutineActive == false) &&
            (Cpu->DpcInterruptRequested == false)) {

            // If the DPC priority is higher than lowest, or we are to deep in the queue depth, retire DPCs immediately.
            if ((Dpc->priority != LOW_PRIORITY) ||
                (DpcData->DpcQueueDepth >= Cpu->MaximumDpcQueueDepth)) {

                // Always mark that an interrupt is needed eventually
                Cpu->DpcInterruptRequested = true;

                // Cannot request an interrupt on DISPATCH_LEVEL already.
                if (MeGetCurrentIrql() < DISPATCH_LEVEL) {
                    // Request an interrupt from HAL.
                    MhRequestSoftwareInterrupt(DISPATCH_LEVEL);
                }
            }
        }
    }

    // Release Lock and Restore IRQL
    MsReleaseSpinlockFromDpcLevel(&DpcData->DpcLock);
    MeLowerIrql(OldIrql);

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
    bool Enable;
    bool Removed = false;

    // Disable interrupts manually since we aren't raising IRQL yet
    Enable = MeDisableInterrupts();

    DpcData = (PDPC_DATA)Dpc->DpcData;

    if (DpcData != NULL) {
        // Acquire Lock
        MsAcquireSpinlockAtDpcLevel(&DpcData->DpcLock);

        // Check if still queued
        if (DpcData == Dpc->DpcData) {
            DpcData->DpcQueueDepth -= 1;
            RemoveEntryList(&Dpc->DpcListEntry);
            Dpc->DpcData = NULL; // Mark as not queued
            Removed = true;
        }

        // Release Lock
        MsReleaseSpinlockFromDpcLevel(&DpcData->DpcLock);
    }

    // Restore Interrupts
    MeEnableInterrupts(Enable);
    return Removed;
}

void
MeRetireDPCs(
    void
)

/*++

    Routine description:

        This function retires the DPC list for the current processor, and also processes timer expiration (first).

    Arguments:

        None.

    Return Values:

        None.

    Notes:
        
        This function is entered with interrupts disabled ( __cli() ), and exits with interrupts disabled.

--*/

{
#ifdef DEBUG
    gop_printf(COLOR_WHITE, "Retiring DPCs!\n");
#endif
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
    uintptr_t TimerHand;
    PPROCESSOR Cpu = MeGetCurrentProcessor();

    DpcData = &Cpu->DpcData;

    // Outer Loop: Process until queue is empty
    do {
        Cpu->DpcRoutineActive = true;

        // Process Timer Expiration -- Unused for now, until we introduce MsWaitForSingleObject (will replace MsWaitForEvent n stuff), and also MeDelayExecutionThread
        /*
        if (Cpu->TimerRequest != 0) {
            TimerHand = Cpu->TimerHand;
            Cpu->TimerRequest = 0;

            __sti(); // Enable interrupts for timer processing
            MeTimerExpiration(TimerHand);
            __cli(); // Disable again
        }
        */
        UNREFERENCED_PARAMETER(TimerHand);

        // Process DPC Queue
        if (DpcData->DpcQueueDepth != 0) {

            // Inner Loop: Pop one, run one
            do {
                // Lock
                MsAcquireSpinlockAtDpcLevel(&DpcData->DpcLock);

                Entry = DpcData->DpcListHead.Flink;

                if (Entry != &DpcData->DpcListHead) {
                    // Remove from List
                    RemoveEntryList(Entry);
                    Dpc = CONTAINING_RECORD(Entry, DPC, DpcListEntry);
                    // Capture Context
                    DeferredRoutine = Dpc->DeferredRoutine;
                    DeferredContext = Dpc->DeferredContext;
                    SystemArgument1 = Dpc->SystemArgument1;
                    SystemArgument2 = Dpc->SystemArgument2;

                    // Changes must be set before others can modify.
                    MmFullBarrier();

                    // Clear DpcData so it can be re-queued inside its own routine
                    Dpc->DpcData = NULL;
                    DpcData->DpcQueueDepth -= 1;

                    // Release Lock
                    MsReleaseSpinlockFromDpcLevel(&DpcData->DpcLock);

                    // Enable Interrupts for execution
                    __sti();

                    // Execute
                    Cpu->CurrentDeferredRoutine = Dpc;
#ifdef DEBUG
                    gop_printf(COLOR_WHITE, "I'm about to execute DPC %p | Routine: %p | SysArg1: %p | SysArg2: %p | Priority: %d\n", Dpc, Dpc->DeferredRoutine, Dpc->SystemArgument1, Dpc->SystemArgument2, Dpc->priority);
#endif
                    DeferredRoutine(Dpc, DeferredContext, SystemArgument1, SystemArgument2);
                    Cpu->CurrentDeferredRoutine = NULL;

                    // Assertion, incase the DPC changed the IRQL level.
                    assert(MeGetCurrentIrql() == DISPATCH_LEVEL);

                    // Disable Interrupts for next loop iteration
                    __cli();

                }
                else {
                    // List was empty
                    MsReleaseSpinlockFromDpcLevel(&DpcData->DpcLock);
                }

            } while (DpcData->DpcQueueDepth != 0);
        }

        Cpu->DpcRoutineActive = false;
        Cpu->DpcInterruptRequested = false;

    } while (DpcData->DpcQueueDepth != 0);

    // Return statement, assert that interrupts are disabled.
    assert(MeAreInterruptsEnabled() == false, "Interrupts must not enabled at DPC Retirement exit");
}

void
MeSetTargetProcessorDpc(
    IN PDPC Dpc,
    IN uint32_t CpuNumber
)

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