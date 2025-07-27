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

/* Declaration to make compiler shut up :) */
void read_context_frame(CTX_FRAME* frame);
void read_interrupt_frame(INT_FRAME* frame);

#ifndef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(x) (void)(x)
#endif

static inline void enqueue(Queue* queue, Thread* thread) {
	thread->nextThread = NULL;
	if (!queue->head) queue->head = thread;
	else queue->tail->nextThread = thread;
	queue->tail = thread;
}

static inline Thread* dequeue(Queue* queue) {
	Thread* thread = queue->head;
	if (!thread) return NULL;

	queue->head = thread->nextThread;
	if (!queue->head) queue->tail = NULL;
	return thread;
}

void InitCPU(void); // defined in kernel.c

extern CPU cpu; // Grab from KERNEL.C

#endif