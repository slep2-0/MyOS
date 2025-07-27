/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:		 Scheduler Implementation.
 */

#include "scheduler.h"

// assembly stubs to save and restore register contexts.
extern void save_context(CTX_FRAME* regs);
extern void restore_context(CTX_FRAME* regs);

// Idle thread, runs when no other is ready.
static Thread idleThread;

void InitScheduler(void) {
	// Init CPU state.
	cpu.schedulerEnabled = true;

	// Init idle thread.
	idleThread.threadState = RUNNING;
	idleThread.timeSlice = 0;
	idleThread.nextThread = NULL;
	cpu.currentThread = &idleThread;

	// The queue is initially empty.
	cpu.readyQueue.head = cpu.readyQueue.tail = NULL;
}

// Enqueue the thread if it's still RUNNABLE.
static void enqueue_runnable(Thread* t) {
	if (t->threadState == RUNNING) {
		t->threadState = READY;
		enqueue(&cpu.readyQueue, t); // Insert the thread into the current CPU ready queue.
	}
}

void Schedule(void) {
	// Must run at DISPATCH_LEVEL. (disabled AUTOMATIC pre-emption, so we don't get disturbed)
	IRQL oldIrql;
	RaiseIRQL(DISPATCH_LEVEL, &oldIrql);

	Thread* prev = cpu.currentThread;
	// Save the thread context.
	save_context(&prev->registers);

	// Re-enqueue if it's still on the RUNNING state. (means we disturbed it at the middle of execution)
	enqueue_runnable(prev);

	// Pick next thread, or idle.
	Thread* next = dequeue(&cpu.readyQueue);
	// If there is nothing, idle.
	if (!next) {
		next = &idleThread;	
	}

	next->threadState = RUNNING;
	cpu.currentThread = next;

	// Restore the next thread's context (in user mode it does iret/ret)
	restore_context(&next->registers);

	// Finally lower the IRQL back so pre-emption can occur.
	LowerIRQL(oldIrql);
}

void CreateThread(Thread* thread, void (*entry)(void), void* stackTop, bool kernelThread) {
	// 1) INT_FRAME
	uint8_t* sp = (uint8_t*)stackTop;
	sp -= sizeof(INT_FRAME);
	INT_FRAME* ifm = (INT_FRAME*)sp;
	ifm->vector = 0;
	ifm->error_code = 0;
	ifm->rip = (uint64_t)entry;
	ifm->cs = kernelThread ? KERNEL_CS : USER_CS;
	ifm->rflags = kernelThread ? INITIAL_RFLAGS : USER_RFLAGS;

	// 2) CTX_FRAME
	sp -= sizeof(CTX_FRAME);
	CTX_FRAME* cfm = (CTX_FRAME*)sp;
	kmemset(cfm, 0, sizeof(*cfm));
	cfm->rsp = (uint64_t)ifm;  // on restore, sets RSP to &INT_FRAME

	// 3) Save into thread struct
	thread->registers = *cfm;
	thread->threadState = READY;
	thread->timeSlice = DEFAULT_TIMESLICE;
	thread->nextThread = NULL;

	// 4) Enqueue
	enqueue(&cpu.readyQueue, thread);
}

void Yield(void) {
	Schedule();
}

void TimerDPC(void) {
	if (cpu.schedulerEnabled) {
		Schedule();
	}
}