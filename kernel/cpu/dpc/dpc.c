/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      DPC Implementation.
 */

#include "dpc.h"
#include "../../bugcheck/bugcheck.h"

volatile DPC* dpcQueueHead = NULL;
volatile DPC* dpcQueueTail = NULL;

void init_dpc_system(void) {
	tracelast_func("init_dpc_system");
	dpcQueueHead = dpcQueueTail = NULL;
}

void MtQueueDPC(volatile DPC* dpc) {
	tracelast_func("queue_dpc");
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

void DispatchDPC(void) {
	tracelast_func("DispatchDPC");
	if (!dpcQueueHead) return;

	IRQL oldIrql;
	MtRaiseIRQL(DISPATCH_LEVEL, &oldIrql);

	// This loop will now complete, because TimerDPC no longer hijacks execution.
	while (dpcQueueHead) {
		volatile DPC* d = dpcQueueHead;
		dpcQueueHead = d->Next;
		if (!dpcQueueHead) {
			dpcQueueTail = NULL;
		}
		if (d->callback || d->callbackWithCtx) {
			if (d->hasCtx) {
				d->callbackWithCtx(d->ctx);
			}
			else {
				d->callback();
			}
		}
	}

	// Lower the IRQL *before* checking the reschedule flag.
	MtLowerIRQL(oldIrql);
}