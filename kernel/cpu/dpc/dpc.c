/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      DPC Implementation.
 */

#include "dpc.h"
#include "../../bugcheck/bugcheck.h"

volatile DPC* dpcQueueHead = NULL;
volatile DPC* dpcQueueTail = NULL;

volatile bool schedule_pending = false;
static SPINLOCK dpc_lock; // SPINLOCK for the dpc, only 1 thread is allowed to use it at a time.

void init_dpc_system(void) {
	tracelast_func("init_dpc_system");
	dpcQueueHead = dpcQueueTail = NULL;
}

void MtQueueDPC(volatile DPC* dpc) {
	tracelast_func("MtQueueDPC");
	if (!dpc) return;

	dpc->Next = NULL;

	// Sorted insertion mechanism by priority (higher priority -> inserted to the head)
	if (!dpcQueueHead) {
		// Starting with empty queue.
		dpcQueueHead = dpcQueueTail = dpc;
		return;
	}
	// not an empty queue -> check priority.
	// check if the priority is highest.
	if (dpc->priority > dpcQueueHead->priority) {
		// Insert at the front (head)
		dpc->Next = dpcQueueHead;
		dpcQueueHead = dpc;
		return;
	}
	// else, find our insertion point.
	volatile DPC* cur = dpcQueueHead;
	// Check each DPC entry for it's priority, and insert the DPC requested accordingly.
	while (cur->Next && cur->Next->priority >= dpc->priority) {
		cur = cur->Next;
	}
	// Singular linked list.
	dpc->Next = cur->Next;
	cur->Next = dpc;
	if (!dpc->Next) {
		dpcQueueTail = dpc;
	}
}

void RetireDPCs(void) {
	tracelast_func("RetireDPCs");
	if (!dpcQueueHead) return;

	IRQL oldIrql;
	IRQL flags;

	// 1) Raise once
	MtRaiseIRQL(DISPATCH_LEVEL, &oldIrql);

	// 2) Acquire lock for the whole drain
	MtAcquireSpinlock(&dpc_lock, &flags);

	// 3) Drain the queue
	while (dpcQueueHead) {
		volatile DPC* d = dpcQueueHead;
		dpcQueueHead = d->Next;
		if (!dpcQueueHead) {
			dpcQueueTail = NULL;
		}
		// release lock so callback can queue new DPCs if needed
		MtReleaseSpinlock(&dpc_lock, flags);

		// STILL at DISPATCH_LEVEL
		if (d->hasCtx)      d->callback.withCtx(d->ctx);
		else if (d->callback.withoutCtx) d->callback.withoutCtx();

		// re-acquire for next pop
		MtAcquireSpinlock(&dpc_lock, &flags);
	}

	// 4) Release lock and lower once
	MtReleaseSpinlock(&dpc_lock, flags);
	MtLowerIRQL(oldIrql);
}
