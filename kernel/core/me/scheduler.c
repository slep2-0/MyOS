/*
    * PROJECT:      MatanelOS Kernel
    * LICENSE:      GPLv3
    * PURPOSE:      Scheduler Implementation.
    */

#include "../../includes/me.h"
#include "../../assert.h"
#include "../../includes/ps.h"
extern PROCESSOR cpus[];

// assembly stubs to save and restore register contexts.
extern void restore_context(TRAP_FRAME* regs);

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
    cfm.rsp = (uint64_t)((uint8_t*)idleStack + IDLE_STACK_SIZE);
    cfm.rip = (uint64_t)kernel_idle_checks;

    // Enable Interrupts on its RFLAGS.
    cfm.rflags |= (1 << 9ULL);

    // Assign the clean context to the idle thread
    idleThread->InternalThread.TrapRegisters = cfm;
    idleThread->InternalThread.ThreadState = THREAD_READY;
    idleThread->InternalThread.TimeSlice = 1; // 1ms
    idleThread->InternalThread.TimeSliceAllocated = 1;
    idleThread->InternalThread.NextThread.Next = NULL;
    idleThread->TID = 0; // Scheduler thread, TID is 0.
    idleThread->InternalThread.StackBase = (void*)cfm.rsp;
    MeGetCurrentProcessor()->currentThread = NULL; // The idle thread would be chosen
    idleThread->CurrentEvent = NULL; // No event.
    idleThread->ParentProcess = &PsInitialSystemProcess;
    PsInitialSystemProcess.MainThread = idleThread;
    MeEnqueueThreadWithLock(&PsInitialSystemProcess.AllThreads, idleThread);

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

// The following function uses CPU Work stealing to steal other CPUs thread (in a queue), if the current thread has no scheduled threads in the queue.
static PITHREAD MeAcquireNextScheduledThread(void) {
    // First, lets try to get from our own queue.
    PETHREAD chosenThread = MeDequeueThreadWithLock(&MeGetCurrentProcessor()->readyQueue);

    if (!chosenThread) {
        // Our own CPU queue is empty, steal from others.
        for (uint32_t i = 0; i < g_cpuCount; i++) {
            if (cpus[i].lapic_ID == MeGetCurrentProcessor()->lapic_ID) continue; // skip ourselves.

            // The reason I used the self pointer here, is because the BSP in the cpus array, is empty except for 4 fields, as its main struct is cpu0, 
            // which is defined at the kernel main, so we access it through self, view SMP.C prepare_percpu for more info.
            Queue* victimQueue = &cpus[i].self->readyQueue;
            if (!victimQueue->head) continue; // skip empty queues

            chosenThread = MeDequeueThreadWithLock(victimQueue);
            if (chosenThread) break;
        }
    }

    // This returns NULL if no thread has been found, or a pointer to the scheduled thread.
    //gop_printf(COLOR_RED, "Returning %p thread | AP: %d\n", chosenThread, thisCPU()->ID); - Spamming this will result in a stack overflow, causing the guard pages to hit on the AP cpus.
    return &chosenThread->InternalThread;
}

NORETURN
void 
Schedule(void) {
    //gop_printf(COLOR_PURPLE, "**In scheduler, IRQL: %d**\n", thisCPU()->currentIrql);

    IRQL oldIrql;
    MeRaiseIrql(DISPATCH_LEVEL, &oldIrql);
    PITHREAD prev = MeGetCurrentProcessor()->currentThread;

    // always check if exists, didn't check and got faulted.
    if (prev && prev->ThreadState == THREAD_TERMINATED) {
        // there was a critical memory issue here where we freed the stack and then pushed an address immediately (to an unmapped stack).
        // just queue a DPC for cleaning both (in order)
        // (it will not pre-empt the scheduler as we are in DISPATCH_LEVEL, scheduling is disabled)
        {
            // TODO: Replace the dynamic allocation with a global variable that is held with a lock, actually - keep the allocation, or make a global variable for each CPU, maybe in their struct?
            // There is a DPC struct for each CPU incase we cant allocate a dynamic one.
            DPC* allocatedDPC = MmAllocatePoolWithTag(NonPagedPool, sizeof(DPC), 'PAER'); // REAP
            if (!allocatedDPC) {
                assert(false);
                MeBugCheck(MANUALLY_INITIATED_CRASH);
            }
            allocatedDPC->CallbackRoutine = CleanStacks;
            allocatedDPC->Arg1 = prev;
            allocatedDPC->Arg2 = allocatedDPC;
            allocatedDPC->Arg3 = false;
            allocatedDPC->Next = NULL; 
            allocatedDPC->priority = MEDIUM_PRIORITY;
            MeQueueDPC(allocatedDPC);
            prev->ThreadState = THREAD_ZOMBIE;
        }
        prev = NULL;
    }

    // All thread's that weren't RUNNING are ignored by the Scheduler. (like BLOCKED threads when waiting or an event, ZOMBIE threads, TERMINATED, etc..)
    if (prev && prev != &MeGetCurrentProcessor()->idleThread->InternalThread && prev->ThreadState == THREAD_RUNNING) {
        // The current thread's registers were already saved in isr_stub. (look after the pushes) (also saved in MtSleepCurrentThread)
        enqueue_runnable(prev);
    }

    PITHREAD next = MeAcquireNextScheduledThread();

    if (!next) {
        next = &MeGetCurrentProcessor()->idleThread->InternalThread;
    }

    next->ThreadState = THREAD_RUNNING;
    MeGetCurrentProcessor()->currentThread = next;
    MeLowerIrql(oldIrql);
    restore_context(&next->TrapRegisters);
    __builtin_unreachable();
}
