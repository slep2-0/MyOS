/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      DPC Implementation.
 */

#include "dpc.h"
#include "../bugcheck/bugcheck.h"
#include "../../intrinsics/atomic.h"

void init_dpc_system(void) {
	tracelast_func("init_dpc_system");
	thisCPU()->DeferredRoutineQueue.dpcQueueHead = thisCPU()->DeferredRoutineQueue.dpcQueueTail = NULL;
}

static inline unsigned int clamp_priority(int priority) {
	if (priority < 0) return 0u;
	if ((unsigned int)priority >= PENDING_DPC_BUCKETS) return PENDING_DPC_BUCKETS - 1;
	return (unsigned int)priority;
}


void MtQueueDPC(DPC* dpc) {
	tracelast_func("MtQueueDPC");
	if (!dpc) return;

    // Try to claim queued flag, if already queued - do nothing.
    uint8_t expected = 0;
    if (!__atomic_compare_exchange_n(&dpc->Queued, &expected, (uint8_t)1, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        return; // Someone already queued this exact DPC.
    }

	struct _DPC_QUEUE* queue = &thisCPU()->DeferredRoutineQueue;

	// normalize priority into bucket index
	unsigned p = clamp_priority(dpc->priority);

	// Lock-free push into pendingHeads[p].
	// Use atomic CAS loop to set dpc->Next -> old_head, then swap old -> new head.
	DPC* old;
	do {
		old = __atomic_load_n(&queue->pendingHeads[p], __ATOMIC_ACQUIRE);
		dpc->Next = old;
		// try to replace old with dpc
	} while (!__atomic_compare_exchange_n(&queue->pendingHeads[p], &old, dpc,
		false, __ATOMIC_RELEASE, __ATOMIC_RELAXED));
}

int MtBeginDpcProcessing(void) {
	CPU* cpu = thisCPU();
	uint8_t old = (uint8_t)InterlockedCompareExchangeU8((volatile uint8_t*)&cpu->DeferredRoutineActive, 1, 0);
	return (old == 0); // success if previous value was 0
}

void MtEndDpcProcessing(void) {
	CPU* cpu = thisCPU();

	// Atomically set back to 0.
	(void)InterlockedExchangeU8((volatile uint8_t*)&cpu->DeferredRoutineActive, 0);
}

static DPC* reverse_list(DPC* head) {
	DPC* prev = NULL;
	while (head) {
		DPC* next = head->Next;
		head->Next = prev;
		prev = head;
		head = next;
	}
	return prev;
}

void RetireDPCs(void) {
    tracelast_func("RetireDPCs");
    struct _DPC_QUEUE* queue = &thisCPU()->DeferredRoutineQueue;

    // quick check: if both main queue and all pending buckets empty, we just return.
    bool any_pending = false;
    for (unsigned i = 0; i < PENDING_DPC_BUCKETS; ++i) {
        if (__atomic_load_n(&queue->pendingHeads[i], __ATOMIC_ACQUIRE)) {
            any_pending = true;
            break;
        }
    }
    if (!queue->dpcQueueHead && !any_pending) return;

    IRQL oldIrql;
    IRQL flags;

    // 1) Raise to DISPATCH_LEVEL
    MtRaiseIRQL(DISPATCH_LEVEL, &oldIrql);

    // 2) Atomically steal pending buckets into local lists (per-priority)
    DPC* stolen[PENDING_DPC_BUCKETS];
    for (unsigned p = 0; p < PENDING_DPC_BUCKETS; ++p) {
        stolen[p] = __atomic_exchange_n(&queue->pendingHeads[p], (DPC*)NULL, __ATOMIC_ACQ_REL);
    }

    // 3) Merge stolen lists into the main queue under the existing spinlock. (from highest to lowest priority)
    MtAcquireSpinlock(&queue->lock, &flags);

    for (int p = (int)PENDING_DPC_BUCKETS - 1; p >= 0; --p) {
        DPC* list = stolen[p];
        if (!list) continue;
        // reverse to preserve FIFO: enqueuers pushed LIFO.
        DPC* chunk = reverse_list(list);

        // append chunk to main queue (fast append using tail)
        if (!queue->dpcQueueHead) {
            queue->dpcQueueHead = chunk;
            // find tail of chunk
            DPC* t = chunk;
            while (t->Next) t = t->Next;
            queue->dpcQueueTail = t;
        }
        else {
            // append
            queue->dpcQueueTail->Next = chunk;
            // update tail
            DPC* t = chunk;
            while (t->Next) t = t->Next;
            queue->dpcQueueTail = t;
        }
    }

    // 4) Now drain the main queue.
    while (queue->dpcQueueHead) {
        DPC* d = queue->dpcQueueHead;
        queue->dpcQueueHead = d->Next;
        if (!queue->dpcQueueHead) {
            queue->dpcQueueTail = NULL;
        }
        // Clear linkage and queued marker while still under lock so callback can requeue.
        d->Next = NULL;
        __atomic_store_n(&d->Queued, (uint8_t)0, __ATOMIC_RELEASE);

        // release lock so callback can queue new DPCs if needed
        MtReleaseSpinlock(&queue->lock, flags);

        // STILL at DISPATCH_LEVEL
        if (d->CallbackRoutine) {
            thisCPU()->CurrentDeferredRoutine = d; // Set deferred routine
            __sti(); // Enable interrupts for routine, so we can issue a bugcheck if too long, and to simply let higher interrupts do their thing.
            d->CallbackRoutine(d, d->Arg1, d->Arg2, d->Arg3);
            __cli(); // Disable.
            thisCPU()->CurrentDeferredRoutine = NULL; // Clear deferred routine.
        }

        // re-acquire for next pop
        MtAcquireSpinlock(&queue->lock, &flags);
    }

    // 5) Release lock and lower IRQL
    MtReleaseSpinlock(&queue->lock, flags);
    MtLowerIRQL(oldIrql);
}
