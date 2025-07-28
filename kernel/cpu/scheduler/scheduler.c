/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      Scheduler Implementation.
 */

#include "scheduler.h"
#include "../../bugcheck/bugcheck.h"

 // assembly stubs to save and restore register contexts.
extern void save_context(CTX_FRAME* regs);
extern void restore_context(CTX_FRAME* regs);

bool reschedule_needed = false;
bool isScheduleDpcQueued = false;

// Idle thread, runs when no other is ready.
Thread idleThread;
// Stack for idle thread
#define IDLE_STACK_SIZE 4096
static uint8_t idleStack[IDLE_STACK_SIZE] __attribute__((aligned(16)));
extern void kernel_idle_checks(void);

// In Scheduler.c
void InitScheduler(void) {
    tracelast_func("InitScheduler");
    cpu.schedulerEnabled = true;

    // --- Build a CLEAN context for the idle thread from scratch ---
    // Do NOT call save_context().

    CTX_FRAME cfm;
    kmemset(&cfm, 0, sizeof(cfm)); // Start with a clean, all-zero context

    // Set only the essential registers for starting the thread
    cfm.rsp = (uint64_t)(idleStack + IDLE_STACK_SIZE);
    cfm.rip = (uint64_t)kernel_idle_checks;

    // Assign the clean context to the idle thread
    idleThread.registers = cfm;
    idleThread.threadState = RUNNING;
    idleThread.nextThread = NULL;

    // Set the idle thread as the one currently running
    cpu.currentThread = &idleThread;

    // The ready queue starts empty
    cpu.readyQueue.head = cpu.readyQueue.tail = NULL;
}

// Enqueue the thread if it's still RUNNABLE.
static void enqueue_runnable(Thread* t) {
    tracelast_func("enqueue_runnable");
    if (t->threadState == RUNNING) {
        t->threadState = READY;
        enqueue(&cpu.readyQueue, t); // Insert into CPU ready queue
    }
}

void Schedule(void) {
    tracelast_func("Schedule");
    if (isScheduleDpcQueued) {
        isScheduleDpcQueued = false;
    }
    IRQL oldIrql;
    RaiseIRQL(DISPATCH_LEVEL, &oldIrql);

    Thread* prev = cpu.currentThread;
    if (prev != &idleThread) {
        save_context(&prev->registers);
        // Only enqueue non-idle
        enqueue_runnable(prev);
    }

    Thread* next = dequeue(&cpu.readyQueue);
    if (!next) {
        next = &idleThread;
    }

    next->threadState = RUNNING;
    cpu.currentThread = next;
    LowerIRQL(oldIrql);
    restore_context(&next->registers);
}


void Yield(void) {
    tracelast_func("Yield");
    Schedule();
}

void TimerDPC(void) {
    tracelast_func("TimerDPC");
    // Don't call Schedule() directly. Just set a flag.
    if (cpu.schedulerEnabled) {
        reschedule_needed = true;
    }
}
