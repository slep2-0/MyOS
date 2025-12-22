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
        return MT_PROCESS_IS_TERMINATING;
    }

    // Create a new thread.
    PETHREAD Thread;
    Status = ObCreateObject(PsThreadType, sizeof(ETHREAD), (void**) & Thread);
    if (MT_FAILURE(Status)) goto Cleanup;

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
    ContextFrame.rip = (uint64_t)EntryPoint;
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
    }

    Thread->ParentProcess = ParentProcess;

    // Create a handle for the thread (and place it in the process's handle table).
    Status = ObCreateHandleForObjectEx(Thread, MT_THREAD_ALL_ACCESS, ThreadHandle, ParentProcess->ObjectTable);
    if (MT_FAILURE(Status)) goto CleanupWithRef;
    
    // Add to list of all threads in the parent process. (acquire its push lock)
    MsAcquirePushLockExclusive(&ParentProcess->ThreadListLock);
    InsertTailList(&ParentProcess->AllThreads, &Thread->ThreadListEntry);
    MsReleasePushLockExclusive(&ParentProcess->ThreadListLock);

    InterlockedIncrementU32((volatile uint32_t*)&ParentProcess->NumThreads);
    Status = MT_SUCCESS;
    // Insert thread to processor queue.
    MeEnqueueThreadWithLock(&MeGetCurrentProcessor()->readyQueue, Thread);

CleanupWithRef:
    // If failure on status, we destroy the thread, if not.
    // We are left with PointerCount == 2 and HandleCount == 1, why? Because if we access the thread and dereference it there still must be left HandleCount == 1 and PointerCount == 1 (the handles)
    // With processes however, its different.
    if (MT_FAILURE(Status)) {
        ObDereferenceObject(Thread);
    }
Cleanup:
    MsReleaseRundownProtection(&ParentProcess->ProcessRundown);
    return Status;
}

MTSTATUS PsCreateSystemThread(ThreadEntry entry, THREAD_PARAMETER parameter, TimeSliceTicks TIMESLICE) {
    if (unlikely(!PsInitialSystemProcess.PID)) return MT_NOT_FOUND; // The system process, somehow, hasn't been setupped yet.
    if (!entry || !TIMESLICE) return MT_INVALID_PARAM;

    // First, allocate a new thread. (using our shiny and glossy new object manager!!!)
    MTSTATUS Status;
    PETHREAD thread; 
    Status = ObCreateObject(PsThreadType, sizeof(ETHREAD), (void*) & thread);
    if (MT_FAILURE(Status)) {
        return Status;
    }

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
    MsReleasePushLockExclusive(&PsInitialSystemProcess.ThreadListLock);

    // Enqueue it into processor. TODO START SUSPENDED?
    MeEnqueueThreadWithLock(&MeGetCurrentProcessor()->readyQueue, thread);
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
    // On thread terminations, we only unlink them from global list, delete stacks, and other
    // BUT WE DO NOT - Delete the ETHREAD, since thats up to the object manager to do so, we do not interfere
    // with its work.
    Thread->InternalThread.ThreadState = THREAD_TERMINATING;
    Thread->ExitStatus = ExitStatus;
    
    // This should exit the thread with the ExitStatus as well (notifying it)

    // We do not change its timeslice unless its a critical operation
    // Look in MiGeneralProtectionFault, thats a case where we do change it.
    // We let it continue until yielding or end of timeslice.

    // Signal all events that the thread is waiting on to execute immediately.
    // Todo parse waitblock.

    // Since this is marked as TERMINATING, the scheduler will dereference the thread in Ob, and from that if
    // the references have reached 0, the Ob will call PsDeleteThread.
    // The scheduler WILL NOT schedule this thread anymore due to its TERMINATION flag.

    // TODO BLOCK APCs from being inserted on thread!

    // The thread does not need to be force suspended, because until a forceful wait occurs (interrupt, syscall, yielding execution via preemption), it can continue running.
    // Forceful termination does not mean we can stop the CPU mid instruction to terminate the thread, because thats stupid.

    // We should queue an APC In the thread to do stuff.

    // The thread would deference its parent thread (if its a user thread)
}

void
PsDeleteThread(
    IN void* Object
)

{
    // This function is called when the reference count for this thread has reached 0 (e.g, it is no longer in use)
    // (No need to call PsTerminateThread, since it set to terminated, and scheduler called dereference)
    // We free everything that the thread uses.
    // All though, before, we should wait for rundown release (so nobody is changing the fields to avoid UAF)
    PETHREAD Thread = (PETHREAD)Object;
    MsWaitForRundownProtectionRelease(&Thread->ThreadRundown);

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