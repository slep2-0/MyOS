/*
    * PROJECT:      MatanelOS Kernel
    * LICENSE:      GPLv3
    * PURPOSE:      Scheduler Implementation.
    */

#include "scheduler.h"
#include "../../bugcheck/bugcheck.h"

// assembly stubs to save and restore register contexts.
extern void restore_context(CTX_FRAME* regs);

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

    CTX_FRAME cfm;
    kmemset(&cfm, 0, sizeof(cfm)); // Start with a clean, all-zero context

    // Set only the essential registers for starting the thread
    cfm.rsp = (uint64_t)(idleStack + IDLE_STACK_SIZE);
    cfm.rip = (uint64_t)kernel_idle_checks;

    // Assign the clean context to the idle thread
    idleThread.registers = cfm;
    idleThread.threadState = READY;
    idleThread.nextThread = NULL;
    idleThread.TID = 0; // Scheduler thread, TID is 0.

    cpu.currentThread = NULL;

    // The ready queue starts empty
    cpu.readyQueue.head = cpu.readyQueue.tail = NULL;
}

// Enqueue the thread if it's still RUNNABLE.
static void enqueue_runnable(Thread* t) {
    tracelast_func("enqueue_runnable");
    if (!t) {
        CTX_FRAME ctx;
        SAVE_CTX_FRAME(&ctx);
        BUGCHECK_ADDITIONALS addt;
        kmemset((void*)&addt, 0, sizeof(BUGCHECK_ADDITIONALS));
        ksnprintf(addt.str, sizeof(addt.str), "Thread was to be enqueued, but it is a null pointer.");
        MtBugcheckEx(&ctx, NULL, NULL_THREAD, &addt, true);
    }
    if (t->threadState == RUNNING) {
        t->threadState = READY;
        MtEnqueueThreadWithLock(&cpu.readyQueue, t); // Insert into CPU ready queue
    }
}

void Schedule(void) {
    tracelast_func("Schedule");
    IRQL oldIrql;
    MtRaiseIRQL(DISPATCH_LEVEL, &oldIrql);

    Thread* prev = cpu.currentThread;

    if (prev && prev != &idleThread && prev->threadState == RUNNING) {
        // The current thread's registers were already saved in isr_stub. (look after the pushes) (also saved in MtSleepCurrentThread)
        enqueue_runnable(prev);
    }

    Thread* next = MtDequeueThreadWithLock(&cpu.readyQueue);

    if (!next) {
        next = &idleThread;
    }
    //gop_printf(COLOR_RED, "The thread's timeslice before change is: %d", next->timeSlice);
    next->threadState = RUNNING;
    next->timeSlice = next->origTimeSlice;
    cpu.currentThread = next;
    __writemsr(IA32_GS_BASE, (uint64_t)next);

    MtLowerIRQL(PASSIVE_LEVEL);
    tracelast_func("Entering restore_context.");
    restore_context(&next->registers);
}
