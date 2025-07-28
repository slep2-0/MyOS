/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      Core CPU Sturcture and definitions.
 */

#ifndef X86_CPU_H
#define X86_CPU_H

// instead of including kernel.h this time which causes problems, ill include each file I need.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cpu_types.h"
#include "irql/irql.h"
#include "dpc/dpc.h"
#include "scheduler/scheduler.h"
#include "thread/thread.h"

/* Declaration to make compiler shut up :) */
void read_context_frame(CTX_FRAME* frame);
void read_interrupt_frame(INT_FRAME* frame);

#ifndef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(x) (void)(x)
#endif

/// Usage: CONTAINING_RECORD(ptr, struct, ptr_member)
/// Example: 
/// CTX_FRAME* ctxframeptr = 0x1234; // Hypothetical address of the pointer.
/// Thread* threadAssociated = CONTAINING_RECORD(ctxframeptr, Thread, ctx); // Note that ctx is the member name for CTX_FRAME in the Thread struct.
#ifndef CONTAINING_RECORD
#define CONTAINING_RECORD(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#endif

static inline void enqueue(Queue* queue, Thread* thread) {
	tracelast_func("enqueue");
	thread->nextThread = NULL;
	if (!queue->head) queue->head = thread;
	else queue->tail->nextThread = thread;
	queue->tail = thread;
}

static inline Thread* dequeue(Queue* queue) {
	tracelast_func("dequeue");
	Thread* thread = queue->head;
	if (!thread) return NULL;

	queue->head = thread->nextThread;
	if (!queue->head) queue->tail = NULL;
	return thread;
}

void InitCPU(void); // defined in kernel.c

extern CPU cpu; // Grab from KERNEL.C

#endif