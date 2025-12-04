/*++

Module Name:

    meinit.c

Purpose:

    This translation unit contains the implementation of core system init routines (executive).

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "../../includes/me.h"

void
MeInitializeProcessor(
    IN PPROCESSOR CPU
)

/*++

    Routine description:

        Initializes the current PROCESSOR struct to default values.

    Arguments:

        [IN]    PPROCESSOR CPU - Pointer to current PROCESSOR struct.

    Return Values:

        None.

--*/

{
    CPU->self = CPU;
    CPU->currentIrql = PASSIVE_LEVEL;
    CPU->schedulerEnabled = NULL; // since NULL is 0, it would be false.
    CPU->currentThread = NULL;
    CPU->readyQueue.head = CPU->readyQueue.tail = NULL;
    // Initialize the DPC Lock & list head.
    CPU->DpcData.DpcLock.locked = 0;
    InitializeListHead(&CPU->DpcData.DpcListHead);

    // Initialize DPC Fields.
    CPU->MaximumDpcQueueDepth = 4; // Baseline.
    CPU->MinimumDpcRate = 1000; // 1000 DPCs per second baseline (TODO DPC Throttling)
    CPU->DpcRequestRate = 0; // Initialized to zero.
    CPU->DpcRoutineActive = false;
    CPU->DpcInterruptRequested = false;
}