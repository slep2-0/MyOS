/*
    * PROJECT:      MatanelOS Kernel
    * LICENSE:      GPLv3
    * PURPOSE:      Scheduler Implementation.
    */

#include "scheduler.h"
#include "../bugcheck/bugcheck.h"
#include "../../assert.h"

// assembly stubs to save and restore register contexts.
extern void restore_context(CTX_FRAME* regs);

// Idle thread, runs when no other is ready.
// Stack for idle thread
extern void kernel_idle_checks(void);
#define IDLE_STACK_SIZE 4096
// In Scheduler.c
void InitScheduler(void) {
    tracelast_func("InitScheduler");
    thisCPU()->schedulerEnabled = true;
    Thread* idleThread = &thisCPU()->idleThread;
    CTX_FRAME cfm;
    kmemset(&cfm, 0, sizeof(cfm)); // Start with a clean, all-zero context

    // Set only the essential registers for starting the thread
    void* idleStack = MtAllocateVirtualMemory(IDLE_STACK_SIZE, 16);
    cfm.rsp = (uint64_t)((uint8_t*)idleStack + IDLE_STACK_SIZE);
    cfm.rip = (uint64_t)kernel_idle_checks;

    // Enable Interrupts on its RFLAGS.
    cfm.rflags |= (1 << 9ULL);

    // Assign the clean context to the idle thread
    idleThread->registers = cfm;
    idleThread->threadState = READY;
    idleThread->timeSlice = 1; // 1ms
    idleThread->origTimeSlice = 1;
    idleThread->nextThread = NULL;
    idleThread->TID = 0; // Scheduler thread, TID is 0.
    idleThread->startStackPtr = (void*)cfm.rsp;
    thisCPU()->currentThread = NULL;

    // The ready queue starts empty
    thisCPU()->readyQueue.head = thisCPU()->readyQueue.tail = NULL;
}

// Enqueue the thread if it's still RUNNING.
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
        t->timeSlice = t->origTimeSlice;
        MtEnqueueThreadWithLock(&thisCPU()->readyQueue, t); // Insert into CPU ready queue
    }
}

extern uint32_t g_cpuCount; // extern the global cpu count. (gotten from smp)
extern CPU cpus[];

// The following function uses CPU Work stealing to steal other CPUs thread (in a queue), if the current thread has no scheduled threads in the queue.
static Thread* MtAcquireNextScheduledThread(void) {
    // First, lets try to get from our own queue.
    Thread* chosenThread = MtDequeueThreadWithLock(&thisCPU()->readyQueue);

    if (!chosenThread) {
        // Our own CPU queue is empty, steal from others.
        for (uint32_t i = 0; i < g_cpuCount; i++) {
            if (cpus[i].lapic_ID == thisCPU()->lapic_ID) continue; // skip ourselves.

            // The reason I used the self pointer here, is because the BSP in the cpus array, is empty except for 4 fields, as its main struct is cpu0, 
            // which is defined at the kernel main, so we access it through self, view SMP.C prepare_percpu for more info.
            Queue* victimQueue = &cpus[i].self->readyQueue;
            if (!victimQueue->head) continue; // skip empty queues

            chosenThread = MtDequeueThreadWithLock(victimQueue);
            if (chosenThread) break;
        }
    }

    // This returns NULL if no thread has been found, or a pointer to the scheduled thread.
    //gop_printf(COLOR_RED, "Returning %p thread | AP: %d\n", chosenThread, thisCPU()->ID); - Spamming this will result in a stack overflow, causing the guard pages to hit on the AP cpus.
    return chosenThread;
}

__attribute__((noreturn))
void Schedule(void) {
    tracelast_func("Schedule");
    //gop_printf(COLOR_PURPLE, "**In scheduler**\n");

    IRQL oldIrql;
    MtRaiseIRQL(DISPATCH_LEVEL, &oldIrql);
    Thread* prev = thisCPU()->currentThread;

    // always check if exists, didn't check and got faulted.
    if (prev && prev->threadState == TERMINATED) {
        // there was a critical memory issue here where we freed the stack and then pushed an address immediately (to an unmapped stack).
        // just queue a DPC for cleaning both (in order)
        // (it will not pre-empt the scheduler as we are in DISPATCH_LEVEL, scheduling is disabled)
        {
            // TODO: Replace the dynamic allocation with a global variable that is held with a lock, actually - keep the allocation, or make a global variable for each CPU, maybe in their struct?
            // There is a DPC struct for each CPU incase we cant allocate a dynamic one.
            DPC* allocatedDPC = MtAllocateVirtualMemory(sizeof(DPC), _Alignof(DPC));
            allocatedDPC->CallbackRoutine = CleanStacks;
            allocatedDPC->Arg1 = prev;
            allocatedDPC->Arg2 = allocatedDPC;
            allocatedDPC->Arg3 = NULL;
            allocatedDPC->Kind = NO_KIND;
            allocatedDPC->Next = NULL; 
            allocatedDPC->priority = MEDIUM_PRIORITY;
            MtQueueDPC(allocatedDPC);
            prev->threadState = ZOMBIE;
        }
        prev = NULL;
    }

    // All thread's that weren't RUNNING are ignored by the Scheduler. (like BLOCKED threads when waiting or an event, ZOMBIE threads, TERMINATED, etc..)
    if (prev && prev != &thisCPU()->idleThread && prev->threadState == RUNNING) {
        // The current thread's registers were already saved in isr_stub. (look after the pushes) (also saved in MtSleepCurrentThread)
        enqueue_runnable(prev);
    }

    Thread* next = MtAcquireNextScheduledThread();

    if (!next) {
        next = &thisCPU()->idleThread;
    }
    next->threadState = RUNNING;
    thisCPU()->currentThread = next;
    MtLowerIRQL(oldIrql);
    tracelast_func("Entering restore_context.");
    restore_context(&next->registers);
    __builtin_unreachable();
}
