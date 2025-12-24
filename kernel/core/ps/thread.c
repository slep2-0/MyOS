#include "../../includes/ps.h"
#include "../../assert.h"
#include "../../includes/mg.h"
#include "../../includes/ob.h"

#define MIN_TID           3u
#define MAX_TID           0xFFFFFFFCu
#define ALIGN_DELTA       3u
#define MAX_FREE_POOL     1024u

#define THREAD_STACK_SIZE (1024*24) // 24 KiB
#define THREAD_ALIGNMENT 16

// Clean exit for a thread—never returns!
static void ThreadExit(void) {
#ifdef DEBUG
    // if you are asking why i dont print the tid, its because i (fucking) hate the gop function
    // so many stack overflows, it makes my blood boil.
    gop_printf(COLOR_RED, "Reached ThreadExit, terminating system thread.\n");
#endif
    // Terminate the thread.
    assert(PsIsKernelThread(PsGetCurrentThread()) == true, "A user thread has entered kernel thread termination.");
    PsTerminateThread(PsGetCurrentThread(), MT_SUCCESS);
    Schedule();
}

// Kernel threads only.
static void ThreadWrapperEx(ThreadEntry thread_entry, THREAD_PARAMETER parameter) {
    // thread_entry(parameters) -> void func(void*)
    thread_entry(parameter); // If thread entry takes no parameters, passing NULL is still fine.
    /// When the thread finishes execution, it will go to ThreadExit to manage cleanup.
    ThreadExit();
}

extern EPROCESS PsInitialSystemProcess;

MTSTATUS
PsCreateThread(
    HANDLE ProcessHandle,
    PHANDLE ThreadHandle,
    ThreadEntry EntryPoint,
    THREAD_PARAMETER ThreadParameter,
    TimeSliceTicks TimeSlice
)

{
    // Checks.
    if (!ProcessHandle || !EntryPoint || !TimeSlice) return MT_INVALID_PARAM;
    MTSTATUS Status;
    PEPROCESS ParentProcess;

    Status = ObReferenceObjectByHandle(ProcessHandle, MT_PROCESS_CREATE_THREAD, PsProcessType, (void**)&ParentProcess, NULL);
    if (MT_FAILURE(Status)) return Status;

    // Acquire process rundown protection.
    if (!MsAcquireRundownProtection(&ParentProcess->ProcessRundown)) {
        // Process is, being terminated?
        ObDereferenceObject(ParentProcess);
        return MT_PROCESS_IS_TERMINATING;
    }

    // Create a new thread.
    PETHREAD Thread;
    Status = ObCreateObject(PsThreadType, sizeof(ETHREAD), (void**) & Thread);
    if (MT_FAILURE(Status)) {
        ObDereferenceObject(ParentProcess);
        MsReleaseRundownProtection(&ParentProcess->ProcessRundown);
        goto Cleanup;
    }

    // Initialize list head.
    InitializeListHead(&Thread->ThreadListEntry);

    // Create a TID for the thread.
    Thread->TID = PsAllocateThreadId(Thread);
    if (Thread->TID == MT_INVALID_HANDLE) goto CleanupWithRef;

    // Create a new stack for the thread's kernel environment.
    Thread->InternalThread.KernelStack = MiCreateKernelStack(false);
    Thread->InternalThread.IsLargeStack = false;
    if (!Thread->InternalThread.KernelStack) goto CleanupWithRef;

    // Create user mode stack. 
    void* BaseAddress = NULL;
    Status = MmCreateUserStack(ParentProcess, &BaseAddress, 0); // 0 to indicate default size.
    if (MT_FAILURE(Status)) goto CleanupWithRef;
    Thread->InternalThread.StackBase = BaseAddress; // Stack grows downward.

    // Setup timeslice.
    Thread->InternalThread.TimeSlice = TimeSlice;
    Thread->InternalThread.TimeSliceAllocated = TimeSlice;

    // Set registers
    TRAP_FRAME ContextFrame;
    kmemset(&ContextFrame, 0, sizeof(TRAP_FRAME));

    ContextFrame.rsp = (uint64_t)Thread->InternalThread.StackBase;
    ContextFrame.rip = (uint64_t)EntryPoint; // Entry point parameter should be removed when mtdll comes around, as it should handle new thread creations
    ContextFrame.rdi = (uint64_t)ThreadParameter;
    ContextFrame.rflags = USER_RFLAGS;
    ContextFrame.cs = USER_CS;
    ContextFrame.ss = USER_SS;
    Thread->InternalThread.TrapRegisters = ContextFrame;
    Thread->SystemThread = false;
    
    // Set state
    Thread->InternalThread.ThreadState = THREAD_READY;
    Thread->InternalThread.ApcState.SavedApcProcess = ParentProcess;

    // Set process's thread properties.
    if (!ParentProcess->MainThread) {
        ParentProcess->MainThread = Thread;
    }
    else {
        // There is a process main thread, this thread's return address must be to ExitThread(), since after the function return of
        // EntryPoint (if it returns), it will POP an invalid value from the stack to return, and so a probable page fault and termination
        // of thread (or process, havent decided yet, look at MiPageFault if it updated)
        // FIXME.
        // (main threads pop back to crt0 runtime, where ExitProcess is ran)
        // So TODO MTDLL.
    }

    Thread->ParentProcess = ParentProcess;

    // Create a handle for the thread (and place it in the process's handle table).
    Status = ObCreateHandleForObjectEx(Thread, MT_THREAD_ALL_ACCESS, ThreadHandle, ParentProcess->ObjectTable);
    if (MT_FAILURE(Status)) goto CleanupWithRef;
    
    // Add to list of all threads in the parent process. (acquire its push lock)
    MsAcquirePushLockExclusive(&ParentProcess->ThreadListLock);

    InsertTailList(&ParentProcess->AllThreads, &Thread->ThreadListEntry);
    ParentProcess->NumThreads++;

    MsReleasePushLockExclusive(&ParentProcess->ThreadListLock);
    Status = MT_SUCCESS;
    // Insert thread to processor queue.
    MeEnqueueThreadWithLock(&MeGetCurrentProcessor()->readyQueue, Thread);

CleanupWithRef:
    // If failure on status, we destroy the thread, if not.
    // We are left with PointerCount == 2 and HandleCount == 1, why? Because if we access the thread and dereference it there still must be left HandleCount == 1 and PointerCount == 1 (the handles)
    // With processes however, its different.
    MsReleaseRundownProtection(&ParentProcess->ProcessRundown);
    if (MT_FAILURE(Status)) {
        ObDereferenceObject(Thread);
        ObDereferenceObject(ParentProcess);
    }
Cleanup:
    return Status;
}

MTSTATUS PsCreateSystemThread(ThreadEntry entry, THREAD_PARAMETER parameter, TimeSliceTicks TIMESLICE, _Out_Opt PETHREAD* OutThread) {
    if (unlikely(!PsInitialSystemProcess.PID)) return MT_NOT_FOUND; // The system process, somehow, hasn't been setupped yet.
    if (!entry || !TIMESLICE) return MT_INVALID_PARAM;

    // First, allocate a new thread. (using our shiny and glossy new object manager!!!)
    MTSTATUS Status;
    PETHREAD thread; 
    Status = ObCreateObject(PsThreadType, sizeof(ETHREAD), (void*) & thread);
    if (MT_FAILURE(Status)) {
        return Status;
    }

    // Initialize list head.
    InitializeListHead(&thread->ThreadListEntry);

    // Create stack
    bool LargeStack = false;
    void* stackStart = MiCreateKernelStack(LargeStack);

    if (!stackStart) {
        // free thread
        ObDereferenceObject(thread);
        return MT_NO_MEMORY;
    }

    uintptr_t StackTop = (uintptr_t)stackStart;

    StackTop &= ~0xF; // Align to 16 bytes (clear lower 4 bits)
    StackTop -= 8; // Decrement by 8 to keep 16-byte alignment. (after pushes)

    thread->InternalThread.StackBase = stackStart; // The stackbase must be the one gotten from MiCreateKernelStack, as freeing with StackTop will result in incorrect arithmetic, and so assertion failure.
    thread->InternalThread.IsLargeStack = LargeStack;
    thread->InternalThread.KernelStack = stackStart;

    TRAP_FRAME* cfm = &thread->InternalThread.TrapRegisters;
    kmemset(cfm, 0, sizeof * cfm);

    // Set our timeslice.
    thread->InternalThread.TimeSlice = TIMESLICE;
    thread->InternalThread.TimeSliceAllocated = TIMESLICE;

    // saved rsp must point to the top (aligned), not sp-8
    cfm->rsp = (uint64_t)StackTop;
    cfm->rip = (uint64_t)ThreadWrapperEx;
    cfm->rdi = (uint64_t)entry; // first argument to ThreadWrapperEx (the entry point)
    cfm->rsi = (uint64_t)parameter; // second arugment to ThreadWrapperEx (the parameter pointer)

    cfm->ss = KERNEL_SS;
    cfm->cs = KERNEL_CS;

    // Create it's RFLAGS with IF bit set to 1.
    cfm->rflags |= (1 << 9ULL);

    // Set it's registers and others.
    thread->InternalThread.TrapRegisters = *cfm;
    thread->InternalThread.ThreadState = THREAD_READY;
    thread->TID = PsAllocateThreadId(thread);
    if (thread->TID == MT_INVALID_HANDLE) {
        ObDereferenceObject(thread);
        return MT_INVALID_HANDLE;
    }
    thread->CurrentEvent = NULL;
    thread->InternalThread.ApcState.SavedApcProcess = &PsInitialSystemProcess;
    thread->SystemThread = true;

    // Process stuffz
    thread->ParentProcess = &PsInitialSystemProcess; // The parent process for the system thread, is the system process.
    // Use the push lock to insert it into AllThreads.
    MsAcquirePushLockExclusive(&PsInitialSystemProcess.ThreadListLock);

    InsertTailList(&PsInitialSystemProcess.AllThreads, &thread->ThreadListEntry);
    PsInitialSystemProcess.NumThreads++;
    
    MsReleasePushLockExclusive(&PsInitialSystemProcess.ThreadListLock);

    // Enqueue it into processor. TODO START SUSPENDED?
    MeEnqueueThreadWithLock(&MeGetCurrentProcessor()->readyQueue, thread);
    if (OutThread) *OutThread = thread;
    return MT_SUCCESS;
}

PETHREAD 
PsGetCurrentThread (void) {
    return CONTAINING_RECORD(MeGetCurrentThread(), ETHREAD, InternalThread);
}

void
PsTerminateThread(
    IN PETHREAD Thread,
    IN MTSTATUS ExitStatus
)

{
    // Non complete function, this should queue a thread Apc to call MtTerminateThread on itself.
    // Or if it is the current thread, we just terminate ourselves.
    if (Thread == PsGetCurrentThread()) {
        // Exit current thread.
        PspExitThread(ExitStatus);
    }

    assert(false, "Termination called upon remote thread, unimplemented. Need APCs");
}

void
PsDeleteThread(
    IN void* Object
)

{
    // This function is called when the reference count for this thread has reached 0 (e.g, it is no longer in use)
    // (it is called after thread termination)
    // We free everything that the ETHREAD uses.
    PETHREAD Thread = (PETHREAD)Object;

    bool IsKernelThread = PsIsKernelThread(Thread);

    // Free TID.
    PsFreeCid(Thread->TID);

    // Free its stack.
    if (IsKernelThread) {
        PsDeferKernelStackDeletion(Thread->InternalThread.KernelStack, Thread->InternalThread.IsLargeStack);
    }
    else {
        // Dereference the parent process, and free its kernel stack..
        PsDeferKernelStackDeletion(Thread->InternalThread.KernelStack, Thread->InternalThread.IsLargeStack);
        ObDereferenceObject(Thread->ParentProcess);
    }

    // When we reach here, the function returns, and the ETHREAD is deleted.
}

NORETURN
void
PspExitThread(
    IN MTSTATUS ExitStatus
)

{
    // This exits the current running thread on the processor.
    PETHREAD Thread = PsGetCurrentThread();
    PEPROCESS CurrentProcess = Thread->InternalThread.ApcState.SavedApcProcess;

    // Cannot terminate this thread if we are attached to a different process (since we would use another process fields)
    if (MeIsAttachedProcess()) {
        MeBugCheckEx(
            INVALID_PROCESS_ATTACH_ATTEMPT,
            (void*)(uintptr_t)CurrentProcess,
            (void*)(uintptr_t)Thread->InternalThread.ApcState.SavedApcProcess,
            (void*)(uintptr_t)Thread,
            NULL
        );
    }

    // Lower IRQL to passive level.
    MeLowerIrql(PASSIVE_LEVEL);

    // We cannot terminate a worker thread.
    if (Thread->WorkerThread) {
        MeBugCheckEx(
            WORKER_THREAD_ATTEMPTED_TERMINATION,
            (void*)(uintptr_t)Thread,
            NULL,
            NULL,
            NULL
        );
    }

    // Todo check for pending APCs, if so bugcheck.

    // Wait for rundown protection release
    MsWaitForRundownProtectionRelease(&Thread->ThreadRundown);

    // Acquire process lock before we modify thread entries.
    MsAcquirePushLockExclusive(&CurrentProcess->ThreadListLock);

    // Decrease thread count and check if we are the last thread (if so, terminate process)
    // No need for interlocked decrement as we hold push lock
    bool LastThread = false;
    if (!(--CurrentProcess->NumThreads)) {
        LastThread = true;
    }
   
    // Remove us from the process thread list.
    PDOUBLY_LINKED_LIST listHead = &CurrentProcess->AllThreads;
    PDOUBLY_LINKED_LIST entry = listHead->Flink;

    while (entry != listHead) {
        PETHREAD iter = CONTAINING_RECORD(entry, ETHREAD, ThreadListEntry);
        if (iter == Thread) {
            // Remove entry
            entry->Blink->Flink = entry->Flink;
            entry->Flink->Blink = entry->Blink;

            // Set entry to point at itself
            InitializeListHead(&Thread->ThreadListEntry);
            break;
        }
        entry = entry->Flink;
    }

    // Release it now.
    MsReleasePushLockExclusive(&CurrentProcess->ThreadListLock);

    if (LastThread && (CurrentProcess->Flags & ProcessBreakOnTermination)) {
        // This is the last thread termination of a critical process, this MUST not happen.
        MeBugCheckEx(
            CRITICAL_PROCESS_DIED,
            (void*)(uintptr_t)CurrentProcess,
            NULL,
            NULL,
            NULL
        );
    }

    if (LastThread) {
        // This is the last thread of the process, we clear its handle table.
        HtDeleteHandleTable(CurrentProcess->ObjectTable);
        CurrentProcess->ObjectTable = NULL;
    }

    // Todo termination ports for a process (so when it dies the user process can like show a message to parent process or sum shit)

    // Todo process the thread's mutexes and waits (unwait all threads waiting on this), along with flushing its APCs.

    // Finally, terminate this thread from the scheduler.
    MeDisableInterrupts();
    Thread->ExitStatus = ExitStatus;
    Thread->InternalThread.ThreadState = THREAD_TERMINATING;

    // Schedule away.
    Schedule();
}