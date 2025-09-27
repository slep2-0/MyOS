/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      DPC Implementation.
 */

#include "dpc.h"
#include "../bugcheck/bugcheck.h"

void init_dpc_system(void) {
	tracelast_func("init_dpc_system");
	thisCPU()->DeferredRoutineQueue.dpcQueueHead = thisCPU()->DeferredRoutineQueue.dpcQueueTail = NULL;
}

void MtQueueDPC(DPC* dpc) {
	tracelast_func("MtQueueDPC"); // NOTE TO SELF: Do not put ANY spinlocks here, we are at high IRQL (definitive bugcheck, if not, deadlock.).
	if (!dpc) return;
	dpc->Next = NULL;
	struct _DPC_QUEUE* queue = &thisCPU()->DeferredRoutineQueue;
	// Sorted insertion mechanism by priority (higher priority -> inserted to the head)
	if (!queue->dpcQueueHead) {
		// Starting with empty queue.
		queue->dpcQueueHead = queue->dpcQueueTail = dpc;
		return;
	}
	// not an empty queue -> check priority.
	// check if the priority is highest.
	if (dpc->priority > queue->dpcQueueHead->priority) {
		// Insert at the front (head)
		dpc->Next = queue->dpcQueueHead;
		queue->dpcQueueHead = dpc;
		return;
	}
	// else, find our insertion point.
	DPC* cur = queue->dpcQueueHead;
	// Check each DPC entry for it's priority, and insert the DPC requested accordingly.
	while (cur->Next && cur->Next->priority >= dpc->priority) {
		cur = cur->Next;
	}
	// Singular linked list.
	dpc->Next = cur->Next;
	cur->Next = dpc;
	if (!dpc->Next) {
		queue->dpcQueueTail = dpc;
	}
}

void RetireDPCs(void) {
	tracelast_func("RetireDPCs");
	struct _DPC_QUEUE* queue = &thisCPU()->DeferredRoutineQueue;
	if (!queue->dpcQueueHead) return;

	IRQL oldIrql;
	IRQL flags;

	// 1) Raise once
	MtRaiseIRQL(DISPATCH_LEVEL, &oldIrql);

	// 2) Acquire lock for the whole drain
	MtAcquireSpinlock(&queue->lock, &flags);

	// 3) Drain the queue
	while (queue->dpcQueueHead) {
		DPC* d = queue->dpcQueueHead;
		queue->dpcQueueHead = d->Next;
		if (!queue->dpcQueueHead) {
			queue->dpcQueueTail = NULL;
		}
		// release lock so callback can queue new DPCs if needed
		MtReleaseSpinlock(&queue->lock, flags);

		// STILL at DISPATCH_LEVEL
		if (d->CallbackRoutine) d->CallbackRoutine(d, d->Arg1, d->Arg2, d->Arg3);

		// re-acquire for next pop
		MtAcquireSpinlock(&queue->lock, &flags);
	}

	// 4) Release lock and lower once
	MtReleaseSpinlock(&queue->lock, flags);
	MtLowerIRQL(oldIrql);
}
