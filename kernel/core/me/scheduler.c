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
extern void restore_user_context_to_user(PETHREAD thread);
extern void restore_user_context_to_kernel(PETHREAD thread);

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
    // A C function entered through a synthetic restore must observe the same
    // ABI stack alignment as if a call instruction had entered it.
    cfm.rsp = (uint64_t)idleStack - 8;
    cfm.rip = (uint64_t)kernel_idle_checks;
    cfm.cs = KERNEL_CS;
    cfm.ss = KERNEL_SS;
    cfm.rflags = INITIAL_RFLAGS;

    idleThread->InternalThread.TrapRegisters = cfm;
    idleThread->InternalThread.StackBase = idleStack;
    idleThread->InternalThread.IsLargeStack = false;
    idleThread->InternalThread.KernelStack = idleStack;

    // Link to the System Process
    PsInitialSystemProcess.MainThread = idleThread;

    MsAcquirePushLockExclusive(&PsInitialSystemProcess.ThreadListLock);
    InsertHeadList(&PsInitialSystemProcess.AllThreads, &idleThread->ThreadListEntry);
    if (PsInitialSystemProcess.NumThreads == UINT32_MAX) {
        MeBugCheckEx(MEMORY_OVERFLOW_DETECTION, &PsInitialSystemProcess,
            idleThread, &PsInitialSystemProcess.AllThreads, RETADDR(0));
    }
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
            if (!chosenThread) continue;

            // A non-NULL owner means the thread has a kernel stack associated
            // with another CPU. Until context switching has an explicit
            // switched-away handshake, only steal never-dispatched threads.
            if (__atomic_load_n(
                &chosenThread->InternalThread.ActiveProcessor,
                __ATOMIC_ACQUIRE
            ) != NULL) {
                MeEnqueueThreadWithLock(victimQueue, chosenThread);
                continue;
            }

            return &chosenThread->InternalThread;
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
    PITHREAD current = MeGetCurrentProcessor()->currentThread;
    PITHREAD IdleThread = &MeGetCurrentProcessor()->idleThread->InternalThread;

    // Check if we need to delete another thread's (safe now, we are at a separate stack)
    if (cpu->ZombieThread) {
        // Drop the reference, we are on another thread's stack
        ObDereferenceObject((void*)cpu->ZombieThread);
        cpu->ZombieThread = NULL;
    }

    if (current && current != IdleThread &&
        current->ThreadState == THREAD_BLOCKING) {
        // Publish BLOCKED before checking WaitStatus. A concurrent wake either
        // changes BLOCKED to READY and queues us, or observes BLOCKING and
        // leaves completion for this CPU to consume below.
        __atomic_store_n(
            &current->ThreadState,
            THREAD_BLOCKED,
            __ATOMIC_SEQ_CST
        );

        if (__atomic_load_n(&current->WaitStatus, __ATOMIC_SEQ_CST) != MT_PENDING) {
            __sync_bool_compare_and_swap(
                &current->ThreadState,
                THREAD_BLOCKED,
                THREAD_RUNNING
            );
        }
    }

    // All thread's that weren't RUNNING are ignored by the Scheduler. (like BLOCKED threads when waiting or an event, ZOMBIE threads, TERMINATED, etc..)
    if (current && current != IdleThread && current->ThreadState == THREAD_TERMINATING) {
        cpu->ZombieThread = current;
        current = NULL;
    }
    else if (current && current != IdleThread && current->ThreadState == THREAD_RUNNING) {
        // The current thread's registers were already saved in isr_stub. (look after the pushes) (also saved in MtSleepCurrentThread)
        enqueue_runnable(current);
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

    if (PsIsKernelThread(PsGetEThreadFromIThread(next))) {
        restore_context(&next->TrapRegisters);
    }
    else {
        // Saved CS is the authoritative resume mode. Interrupt frames provide
        // it directly, and MsYieldExecution records KERNEL_CS for a blocked
        // syscall continuation. Address ranges are not execution-state.
        if ((next->TrapRegisters.cs & 3) == 0) {
            restore_user_context_to_kernel(PsGetEThreadFromIThread(next));
        }
        else {
            restore_user_context_to_user(PsGetEThreadFromIThread(next));
        }
    }
    UNREACHABLE_CODE();
}
