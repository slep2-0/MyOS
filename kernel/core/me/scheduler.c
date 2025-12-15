/*
    * PROJECT:      MatanelOS Kernel
    * LICENSE:      GPLv3
    * PURPOSE:      Scheduler Implementation.
    */

#include "../../includes/me.h"
#include "../../assert.h"
#include "../../includes/ps.h"
#include "../../includes/mg.h"
#include "../../includes/ob.h"
extern PROCESSOR cpus[];

// assembly stubs to save and restore register contexts.
extern void restore_context(TRAP_FRAME* regs);
extern void restore_user_context(PETHREAD thread);

// Idle thread, runs when no other is ready.
// Stack for idle thread
extern void kernel_idle_checks(void);
#define IDLE_STACK_SIZE 4096

extern EPROCESS PsInitialSystemProcess;

// In Scheduler.c
void InitScheduler(void) {
    MeGetCurrentProcessor()->schedulerEnabled = true;
    MeGetCurrentProcessor()->idleThread = MmAllocatePoolWithTag(NonPagedPool, sizeof(ETHREAD), 'ELDI');
    PETHREAD idleThread = MeGetCurrentProcessor()->idleThread;
    
    TRAP_FRAME cfm;
    kmemset(&cfm, 0, sizeof(cfm)); // Start with a clean, all-zero context

    // Set only the essential registers for starting the thread
    void* idleStack = MiCreateKernelStack(false);
    assert(idleStack != NULL);
    cfm.rsp = (uint64_t)idleStack;
    cfm.rip = (uint64_t)kernel_idle_checks;

    // Enable Interrupts on its RFLAGS.
    cfm.rflags |= (1 << 9ULL);

    // Assign the clean context to the idle thread
    idleThread->InternalThread.TrapRegisters = cfm;
    idleThread->InternalThread.ThreadState = THREAD_READY;
    idleThread->InternalThread.TimeSlice = 1; // 1ms
    idleThread->InternalThread.TimeSliceAllocated = 1;
    InitializeListHead(&idleThread->ThreadListEntry);
    idleThread->TID = 0; // Idle thread, TID is 0.
    idleThread->InternalThread.StackBase = (void*)cfm.rsp;
    idleThread->InternalThread.IsLargeStack = false;
    MeGetCurrentProcessor()->currentThread = NULL; // The idle thread would be chosen
    idleThread->CurrentEvent = NULL; // No event.
    idleThread->ParentProcess = &PsInitialSystemProcess;
    PsInitialSystemProcess.MainThread = idleThread;
    InsertHeadList(&PsInitialSystemProcess.AllThreads, &idleThread->ThreadListEntry);

    // The ready queue starts empty
    MeGetCurrentProcessor()->readyQueue.head = MeGetCurrentProcessor()->readyQueue.tail = NULL;
}

// Enqueue the thread if it's still RUNNING.
static void enqueue_runnable(PITHREAD t) {
    assert((t) != 0);
    if (t->ThreadState == THREAD_RUNNING) {
        t->ThreadState = THREAD_READY;
        t->TimeSlice = t->TimeSliceAllocated;
        MeEnqueueThreadWithLock(&MeGetCurrentProcessor()->readyQueue, PsGetEThreadFromIThread(t)); // Insert into CPU ready queue
    }
}

extern uint32_t g_cpuCount; // extern the global cpu count. (gotten from smp)
extern bool smpInitialized;

// The following function uses CPU Work stealing to steal other CPUs thread (in a queue), if the current thread has no scheduled threads in the queue.
static PITHREAD MeAcquireNextScheduledThread(void) {
    // First, lets try to get from our own queue.
    PETHREAD chosenThread = MeDequeueThreadWithLock(&MeGetCurrentProcessor()->readyQueue);
    if (chosenThread) return &chosenThread->InternalThread;

#ifndef MT_UP
    if (smpInitialized) {
        // Our own CPU queue is empty, steal from others.
        for (uint32_t i = 0; i < g_cpuCount; i++) {
            if (cpus[i].lapic_ID == MeGetCurrentProcessor()->lapic_ID) continue; // skip ourselves.

            // The reason I used the self pointer here, is because the BSP in the cpus array, is empty except for 4 fields, as its main struct is cpu0, 
            // which is defined at the kernel main, so we access it through self, view SMP.C prepare_percpu for more info.
            Queue* victimQueue = &cpus[i].self->readyQueue;
            if (!victimQueue->head) continue; // skip empty queues

            chosenThread = MeDequeueThreadWithLock(victimQueue);
            // Found a suitable thread, return it.
            if (chosenThread) return &chosenThread->InternalThread;
        }
    }
#endif

    // No thread found.
    return NULL;
}

NORETURN
void 
Schedule(void) {
    //gop_printf(COLOR_PURPLE, "**In scheduler, IRQL: %d**\n", MeGetCurrentIrql());
    IRQL oldIrql;
    MeRaiseIrql(DISPATCH_LEVEL, &oldIrql); // Prevents scheduling re-entrance.

    PPROCESSOR cpu = MeGetCurrentProcessor();
    PITHREAD prev = MeGetCurrentProcessor()->currentThread;
    PITHREAD IdleThread = &MeGetCurrentProcessor()->idleThread->InternalThread;

    // Check if we need to delete another thread's (safe now, we are at a separate stack)
    if (cpu->ZombieThread) {
        // Drop the reference, we are on another thread's stack.
        ObDereferenceObject((void*)cpu->ZombieThread);
        cpu->ZombieThread = NULL;
    }

    // All thread's that weren't RUNNING are ignored by the Scheduler. (like BLOCKED threads when waiting or an event, ZOMBIE threads, TERMINATED, etc..)
    if (prev && prev != IdleThread && prev->ThreadState == THREAD_TERMINATING) {
        cpu->ZombieThread = prev;
        prev = NULL;
    }
    else if (prev && prev != IdleThread && prev->ThreadState == THREAD_RUNNING) {
        // The current thread's registers were already saved in isr_stub. (look after the pushes) (also saved in MtSleepCurrentThread)
        enqueue_runnable(prev);
    }

    PITHREAD next = MeAcquireNextScheduledThread();

    if (!next) {
        next = IdleThread;
    }

    next->ThreadState = THREAD_RUNNING;
    MeGetCurrentProcessor()->currentThread = next;
    MeLowerIrql(oldIrql);
    if (PsIsKernelThread(PsGetEThreadFromIThread(next))) {
        restore_context(&next->TrapRegisters);
    }
    else {
        // User thread
        restore_user_context(PsGetEThreadFromIThread(next));
    }
    __builtin_unreachable();
}
