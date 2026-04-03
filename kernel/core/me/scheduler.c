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
extern void restore_user_context_withswapgs(PETHREAD thread);
extern void restore_user_context_withoutswapgs(PETHREAD thread);

// Idle thread, runs when no other is ready.
// Stack for idle thread
extern void kernel_idle_checks(void);
#define IDLE_STACK_SIZE 4096

extern EPROCESS PsInitialSystemProcess;

// In Scheduler.c
void InitScheduler(void) {
    MeGetCurrentProcessor()->schedulerEnabled = true;

    PETHREAD idleThread = NULL;
    MTSTATUS Status = ObCreateObject(PsThreadType, sizeof(ETHREAD), (void**)&idleThread);
    if (MT_FAILURE(Status)) {
        // If we can't allocate the idle thread during boot, the system is toast.
        MeBugCheckEx(MEMORY_LIMIT_REACHED, NULL, NULL, NULL, NULL);
    }

    MeGetCurrentProcessor()->idleThread = idleThread;

    // Use the unified helper to set up APC lists, PIDs, and states
    PspInitializeThread(idleThread, &PsInitialSystemProcess, 1); // 1ms timeslice

    // Idle thread specific overrides
    idleThread->TID = 0;
    idleThread->SystemThread = true;

    // Set up the execution context
    void* idleStack = MiCreateKernelStack(false);
    assert(idleStack != NULL);

    TRAP_FRAME cfm;
    kmemset(&cfm, 0, sizeof(cfm));
    cfm.rsp = (uint64_t)idleStack;
    cfm.rip = (uint64_t)kernel_idle_checks;
    cfm.rflags |= (1 << 9ULL); // Ensure interrupts are enabled for the idle loop

    idleThread->InternalThread.TrapRegisters = cfm;
    idleThread->InternalThread.StackBase = (void*)cfm.rsp;
    idleThread->InternalThread.IsLargeStack = false;
    idleThread->InternalThread.KernelStack = idleStack;

    // Link to the System Process
    PsInitialSystemProcess.MainThread = idleThread;

    MsAcquirePushLockExclusive(&PsInitialSystemProcess.ThreadListLock);
    InsertHeadList(&PsInitialSystemProcess.AllThreads, &idleThread->ThreadListEntry);
    PsInitialSystemProcess.NumThreads++; // Maintain accurate thread count
    MsReleasePushLockExclusive(&PsInitialSystemProcess.ThreadListLock);

    // Reset Scheduler state
    // We do NOT call MeEnqueueThread here, the idle thread remains outside the ready queue.
    MeGetCurrentProcessor()->currentThread = NULL;
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
        // Drop the reference, we are on another thread's stack
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
    next->ActiveProcessor = cpu; // Set the thread's current CPU as this.
    MeGetCurrentProcessor()->currentThread = next;

    // Check if this thread has any APCs queued to it
    // If so, request the interrupt.
    IRQL apcQueueIrql;
    MsAcquireSpinlock(&next->ApcQueueLock, &apcQueueIrql);
    if (next->ApcListHead.Flink != &next->ApcListHead) {
        cpu->ApcInterruptRequested = true;
    }
    MsReleaseSpinlock(&next->ApcQueueLock, apcQueueIrql);

    // Disable interrupts, we must not scheduled away now.
    MeDisableInterrupts();
    
    // Lower IRQL back to its original value.
    MeLowerIrql(oldIrql);

    // Hi matanel, if you ever encounter failures here, like if it goes to restore_user_context as a system thread
    // please check that you made the same changed to InitScheduler as you made in PsCreateSystemThread, for example, Thread->SystemThread was false in the idle thread, because I forgot to set
    // that flag in its initilization, even though I was sure its on (for normal threads that is), because in PsCreateSystemThreads it was indeed = true.
    if (PsIsKernelThread(PsGetEThreadFromIThread(next))) {
        restore_context(&next->TrapRegisters);
    }
    else {
        // User thread - Check if we should execute swapgs, because if we will execute it when we return to kernel RIP (like in a syscall for example), then GS would point to user mode.
        // Check RIP, if its in kernel then WE DO NOT swap.
        // This works ONLY when there is a CLI call before doing swapgs, since we could prepare to return to user mode, and then we return with an opposite GS.
        // ACTUALLY DO NOT create a trap frame GS, (only the offset), this should be handled carefully
        // I do not know what the fuck do i do..

        // Note that this uses MmSystemRangeStart which is PhysicalMemoryOffset (which is the start of the kernel space in the 64bit addr space)
        // It's fine. (no need to use KernelVaStart)
        if (next->TrapRegisters.rip >= MmSystemRangeStart) {
            restore_user_context_withoutswapgs(PsGetEThreadFromIThread(next));
        }
        else {
            restore_user_context_withswapgs(PsGetEThreadFromIThread(next));
        }
    }
    UNREACHABLE_CODE();
}
