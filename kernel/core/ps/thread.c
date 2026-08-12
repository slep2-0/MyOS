#include "../../includes/ps.h"
#include "../../assert.h"
#include "../../includes/mg.h"
#include "../../includes/ob.h"
#include "../../includes/mt.h"

#define MIN_TID           3u
#define MAX_TID           0xFFFFFFFCu
#define ALIGN_DELTA       3u
#define MAX_FREE_POOL     1024u

#define THREAD_STACK_SIZE (1024*24) // 24 KiB
#define THREAD_ALIGNMENT 16
#define MTDLL_THREAD_ROUTINE "LdrInitializeThread"

// Clean exit for a thread—never returns!
static void ThreadExit(void)

/*++

    Routine description:

        Terminates the current system thread after its entry routine returns.

    Arguments:

        None.

    Return Values:

        None.

--*/

{
#ifdef DEBUG
    gop_printf(COLOR_RED, "Reached ThreadExit, terminating system thread tid %d.\n", PsGetCurrentThread()->TID);
#endif
    // Terminate the thread.
    assert(PsIsKernelThread(PsGetCurrentThread()) == true, "A user thread has entered kernel thread termination.");
    PsTerminateThread(PsGetCurrentThread(), MT_SUCCESS);
    Schedule();
}

// Kernel threads only.
// This should change i guess
static void ThreadWrapperEx(ThreadEntry thread_entry, THREAD_PARAMETER parameter)

/*++

    Routine description:

        Invokes a system-thread entry routine and routes a normal return through thread exit.

    Arguments:

        [IN] thread_entry - Entry routine invoked by the new system thread.
        [IN] parameter - Context passed to the callback or worker.

    Return Values:

        None.

--*/

{
    // thread_entry(parameters) -> void func(void*)
    thread_entry(parameter); // If thread entry takes no parameters, passing NULL is still fine.
    /// When the thread finishes execution, it will go to ThreadExit to manage cleanup.
    ThreadExit();
}

extern EPROCESS PsInitialSystemProcess;

static
void
PspThreadRundown(
    IN PAPC Apc
)

/*++

    Routine description:

        Releases a dynamically allocated APC during thread rundown.

    Arguments:

        [IN] Apc - APC object associated with the callback.

    Return Values:

        None.

--*/

{
    // Free it.
    MmFreePool(Apc);
}

static void
PspRundownThreadApcs(
    IN PITHREAD Thread
)

/*++

    Routine description:

        Removes and runs down every APC still queued to a terminating thread.

    Arguments:

        [IN] Thread - Thread affected by the operation.

    Return Values:

        None.

--*/

{
    DOUBLY_LINKED_LIST RundownList;
    InitializeListHead(&RundownList);

    IRQL OldIrql;
    MsAcquireSpinlock(&Thread->ApcQueueLock, &OldIrql);

    // Immediately set Queuable to false
    // This is also set when beginning a thread exit, which isnt a problem.
    // This is so NO APCs will be queued when thread should be terminating.
    InterlockedStore(&Thread->ApcQueueable, false);

    bool WasSuspended = Thread->SuspendCount != 0;
    bool SuspendApcQueued = Thread->SuspendAPC.Inserted != 0;
    bool SuspendApcActive = Thread->SuspendApcActive;
    Thread->SuspendCount = 0;

    if (SuspendApcQueued) {
        // Rundown will remove the queued APC, so no invocation remains active.
        assert(SuspendApcActive);
        Thread->SuspendApcActive = false;
    }

    for (uint32_t Mode = KernelMode; Mode <= UserMode; Mode++) {
        PDOUBLY_LINKED_LIST ApcList =
            &Thread->ApcState.ApcListHead[Mode];
        PDOUBLY_LINKED_LIST Entry;

        while ((Entry = RemoveHeadList(ApcList)) != NULL) {
            PAPC Apc = CONTAINING_RECORD(Entry, APC, ApcListEntry);
            Apc->Inserted = 0;
            InsertTailList(&RundownList, Entry);
        }
    }

    InterlockedStoreRelease(&Thread->ApcState.KernelApcPending, false);
    InterlockedStoreRelease(&Thread->ApcState.UserApcPending, false);
    MsReleaseSpinlock(&Thread->ApcQueueLock, OldIrql);

    for (;;) {
        PDOUBLY_LINKED_LIST Entry = RemoveHeadList(&RundownList);
        if (!Entry) break;

        PAPC Apc = CONTAINING_RECORD(Entry, APC, ApcListEntry);
        if (Apc->RundownRoutine) Apc->RundownRoutine(Apc);
    }

    if (WasSuspended && SuspendApcActive && !SuspendApcQueued) {
        /*
         * The suspend APC has already left its queue and may be executing or
         * blocked on the private semaphore. Give it one permit so it can see
         * SuspendCount == 0 and ApcQueueable == false, clear its active state,
         * and return through the termination path.
         */
        MsReleaseSemaphore(&Thread->SuspendSemaphore, 1);
    }
}

static void
PspBeginThreadExit(
    IN PETHREAD Thread
)

/*++

    Routine description:

        Publishes thread termination and prevents new APC insertion.

    Arguments:

        [IN] Thread - Thread affected by the operation.

    Return Values:

        None.

--*/

{
    uint32_t State = InterlockedLoadAcquire(
        &Thread->TerminationState
    );

    for (;;) {
        // The installing CPU publishes the APC while holding ApcQueueLock and
        // changes this state before it can request delivery.
        if (State == ThreadTerminationInstalling) {
            __pause();
            State = InterlockedLoadAcquire(
                &Thread->TerminationState
            );
            continue;
        }

        if (State == ThreadTerminationExiting) {
            MeBugCheckEx(SCHEDULER_FAILURE, Thread,
                (void*)(uintptr_t)State, RETADDR(0), NULL);
        }

        uint32_t Observed = InterlockedCompareExchangeU32(
            &Thread->TerminationState,
            ThreadTerminationExiting,
            State
        );
        if (Observed == State) break;
        State = Observed;
    }

    // No new APC can be inserted after Exiting is visible. This also removes a
    // queued termination APC when natural thread exit won the race.
    PspRundownThreadApcs(&Thread->InternalThread);
}

static
void
PspThreadTerminationRoutine(
    struct _APC* Apc, PNORMAL_ROUTINE* NormalRoutine, void** NormalContext, void** SystemArgument1, void** SystemArgument2
)

/*++

    Routine description:

        Performs thread termination from the target thread APC context.

    Arguments:

        [IN] Apc - APC object associated with the callback.
        [IN] NormalRoutine - Normal APC routine to invoke.
        [IN] NormalContext - Context passed to the APC normal routine.
        [IN] SystemArgument1 - First system argument supplied to the DPC.
        [IN] SystemArgument2 - Second system argument supplied to the DPC.

    Return Values:

        None.

--*/

{
    UNREFERENCED_PARAMETER(NormalContext);
    UNREFERENCED_PARAMETER(NormalRoutine);
    UNREFERENCED_PARAMETER(SystemArgument2);
    // Call PspExitThread, we are terminating this thread.
    // Rundown would delete this APC, since its allocated.
    MTSTATUS ExitStatus = (MTSTATUS)(uintptr_t)*SystemArgument1;

    Apc->RundownRoutine(Apc);

    // Set APC Routine as non active in the current CPU, as PspExitThread is a NORETURN
    MeGetCurrentProcessor()->ApcRoutineActive = false;

    PspExitThread(ExitStatus);
}

static
void
PspSuspendThreadApc(
    void* NormalContext, void* SystemArgument1, void* SystemArgument
)

/*++

    Routine description:

        Blocks the target thread on its private suspend semaphore.

    Arguments:

        [IN] NormalContext - Context passed to the APC normal routine.
        [IN] SystemArgument1 - First system argument supplied to the DPC.
        [IN] SystemArgument - System argument passed to the APC.

    Return Values:

        None.

--*/

{
    // The suspend APC does not use its two system arguments.
    UNREFERENCED_PARAMETER(SystemArgument); UNREFERENCED_PARAMETER(SystemArgument1);

    PITHREAD Thread = (PITHREAD)NormalContext;

    for (;;) {
        /*
         * Run in the target thread's context and wait behind its private resume
         * gate. If resume arrived before this APC, the stored permit makes this
         * wait return immediately. Otherwise the target parks here.
         */
        MTSTATUS Status = MsWaitForSingleObject(
            &Thread->SuspendSemaphore,
            KernelMode,
            false,
            MT_INFINITE
        );
        if (Status != MT_SUCCESS) {
            MeBugCheckEx(
                WAIT_STATE_FAILURE,
                Thread,
                &Thread->SuspendSemaphore,
                (void*)(uintptr_t)Status,
                (void*)PspSuspendThreadApc
            );
        }

        /*
         * Resume and a new suspend can race while this APC is waking. Inspect
         * the logical count under the same lock used by both operations.
         */
        IRQL OldIrql;
        MsAcquireSpinlock(&Thread->ApcQueueLock, &OldIrql);

        if (Thread->SuspendCount == 0 || !Thread->ApcQueueable) {
            /*
             * No suspension remains, or termination is tearing this thread
             * down. Relinquish ownership only while holding ApcQueueLock so a
             * simultaneous 0 -> 1 suspend either reuses us or queues a new APC.
             */
            Thread->SuspendApcActive = false;
            MsReleaseSpinlock(&Thread->ApcQueueLock, OldIrql);
            return;
        }

        /*
         * Another suspend changed the count back above zero before this wake
         * completed. Keep ownership and wait again instead of queuing a second
         * invocation of the embedded APC.
         */
        MsReleaseSpinlock(&Thread->ApcQueueLock, OldIrql);
    }
}

void 
PspInitializeThread(
    PETHREAD Thread, PEPROCESS Process, TimeSliceTicks TimeSlice
)

/*++

    Routine description:

        Initializes dispatcher, APC, wait, ownership, and scheduling state for a thread.

    Arguments:

        [IN] Thread - Thread affected by the operation.
        [IN] Process - Process affected by the operation.
        [IN] TimeSlice - Scheduler quantum assigned to the thread.

    Return Values:

        None.

--*/

{
    // Basic linking
    InitializeListHead(&Thread->ThreadListEntry);
    InitializeListHead(
        &Thread->InternalThread.ApcState.ApcListHead[KernelMode]
    );
    InitializeListHead(
        &Thread->InternalThread.ApcState.ApcListHead[UserMode]
    );
    InitializeListHead(&Thread->InternalThread.WaitBlock.ObjectListEntry);
    InitializeListHead(&Thread->InternalThread.WaitBlock.TimerListEntry);
    InitializeListHead(&Thread->InternalThread.OwnedMutexListHead);
    Thread->InternalThread.OwnedMutexesListLock.locked = 0;
    Thread->InternalThread.WaitBlock.Object = NULL;
    Thread->InternalThread.WaitBlock.WaitReason = WaitReasonNone;
    Thread->InternalThread.WaitBlock.WakeupTime = 0;
    Thread->InternalThread.WaitStatus = MT_SUCCESS;
    Thread->InternalThread.WaitCompletionComplete = true;
    Thread->InternalThread.UserApcActive = false;
    Thread->InternalThread.ApcQueueLock.locked = 0;
    Thread->InternalThread.ApcState.KernelApcInProgress = false;
    Thread->InternalThread.ApcState.KernelApcPending = false;
    Thread->InternalThread.ApcState.UserApcPending = false;
    Thread->TerminationState = ThreadTerminationNone;
    Thread->ExitStatus = MT_PENDING; // STILL_ACTIVE in Usermode

    // Exceptions
    Thread->InternalThread.UserExceptionPending = false;
    Thread->InternalThread.UserExceptionActive = false;
    kmemset(
        &Thread->InternalThread.PendingExceptionRecord,
        0,
        sizeof(Thread->InternalThread.PendingExceptionRecord)
    );

    // Process association
    Thread->ParentProcess = Process;
    Thread->InternalThread.ApcState.SavedApcProcess = Process;
    Thread->PID = Process->PID;

    // Scheduling defaults
    Thread->InternalThread.TimeSlice = TimeSlice;
    Thread->InternalThread.TimeSliceAllocated = TimeSlice;
    Thread->InternalThread.ThreadState = THREAD_READY;

    // The semaphore stores at most one early-resume permit. Its initial zero
    // closes the gate, but SuspendCount == 0 means no APC will wait on it yet.
    MsInitializeSemaphore(&Thread->InternalThread.SuspendSemaphore, 0, 1);
    Thread->InternalThread.SuspendCount = 0;
    Thread->InternalThread.SuspendApcActive = false;
   
    // Initialize SuspendAPC
    MeInitializeApc(&Thread->InternalThread.SuspendAPC, &Thread->InternalThread, KernelMode, NULL, NULL, PspSuspendThreadApc, Thread);

    // Kernel & Special APCs
    Thread->InternalThread.KernelApcDisable = 0;
    Thread->InternalThread.SpecialApcDisable = 0;
    Thread->InternalThread.ApcQueueable = true;

    // Push locks
    MsInitializePushLock(&Thread->ThreadLock);

    // Dispatcher Header Initialization
    MsInitializeDispatcherHeader(&Thread->InternalThread.Header, 0, DispatcherThread);
}

MTSTATUS
PsCreateThread(
    PEPROCESS Process,
    PHANDLE ThreadHandle,
    THREAD_START_ROUTINE EntryPoint,
    THREAD_PARAMETER ThreadParameter,
    TimeSliceTicks TimeSlice,
    ThreadEntry MtdllEntrypoint
)

/*++

    Routine description:

        Creates and initializes a thread in a process.

    Arguments:

        [IN] Process - Process affected by the operation.
        [OUT] ThreadHandle - Receives the created thread handle.
        [IN] EntryPoint - Initial instruction address of the process or thread.
        [IN] ThreadParameter - Context passed to the thread entry routine.
        [IN] TimeSlice - Scheduler quantum assigned to the thread.
        [IN] MtdllEntrypoint - Mapped MTDLL initialization entry point.

    Return Values:

        MT_SUCCESS on success, or an error status describing the failure.

--*/

{
    // Checks.
    if (!Process || !ThreadHandle || !EntryPoint || !TimeSlice) {
        return MT_INVALID_PARAM;
    }

    *ThreadHandle = MT_INVALID_HANDLE;
    MTSTATUS Status = MT_GENERAL_FAILURE;
    PEPROCESS ParentProcess = Process;
    PETHREAD Thread = NULL;
    void* BaseAddress = NULL;
    size_t StackSize = MI_DEFAULT_USER_STACK_SIZE;
    PTEB Teb = NULL;
    bool ThreadHandleCreated = false;
    bool ParentReferenced = false;
    bool ParentReferenceTransferred = false;
    bool RundownAcquired = false;
    bool IsMainThread = false;

    // Reference parent.
    if (!ObReferenceObject(ParentProcess)) {
        return MT_PROCESS_IS_TERMINATING;
    }
    ParentReferenced = true;

    // Acquire process rundown protection.
    if (!MsAcquireRundownProtection(&ParentProcess->ProcessRundown)) {
        // Process is, being terminated?
        ObDereferenceObject(ParentProcess);
        return MT_PROCESS_IS_TERMINATING;
    }
    RundownAcquired = true;

    if (InterlockedLoadAcquire(&ParentProcess->Flags) &
        (ProcessBeingTerminated | ProcessBeingDeleted)) {
        Status = MT_PROCESS_IS_TERMINATING;
        goto Cleanup;
    }

    // A loader entry is supplied only for the initial process thread. Normal
    // MtCreateThread calls intentionally pass NULL and enter through the
    // already-loaded LdrInitializeThread export instead.
    IsMainThread = MtdllEntrypoint != NULL;
    MsAcquirePushLockShared(&ParentProcess->ThreadListLock);
    bool HasMainThread = ParentProcess->MainThread != NULL;
    MsReleasePushLockShared(&ParentProcess->ThreadListLock);

    if (IsMainThread == HasMainThread) {
        Status = IsMainThread ? MT_INVALID_PARAM : MT_PROCESS_IS_TERMINATING;
        goto Cleanup;
    }

    // Create a new thread.
    Status = ObCreateObject(PsThreadType, sizeof(ETHREAD), (void**) & Thread);
    if (MT_FAILURE(Status)) goto Cleanup;

    PspInitializeThread(Thread, ParentProcess, TimeSlice);
    Thread->SystemThread = false;
    ParentReferenceTransferred = true;

    // Create a TID for the thread.
    Thread->TID = PsAllocateThreadId(Thread);
    if (Thread->TID == MT_INVALID_HANDLE) {
        Status = MT_INVALID_HANDLE;
        goto Cleanup;
    }

    // Write the PID into the thread.
    Thread->PID = ParentProcess->PID;

    // Create a new stack for the thread's kernel environment.
    Thread->InternalThread.KernelStack = MiCreateKernelStack(false);
    Thread->InternalThread.IsLargeStack = false;
    if (!Thread->InternalThread.KernelStack) {
        Status = MT_NO_MEMORY;
        goto Cleanup;
    }

    // Create user mode stack. 
    Status = MmCreateUserStack(ParentProcess, &BaseAddress, StackSize);
    if (MT_FAILURE(Status)) goto Cleanup;
    Thread->InternalThread.StackBase = BaseAddress; // Stack grows downward.
    Thread->UserStackSize = StackSize;

    // Setup timeslice.
    Thread->InternalThread.TimeSlice = TimeSlice;
    Thread->InternalThread.TimeSliceAllocated = TimeSlice;

    // Set registers
    TRAP_FRAME ContextFrame;
    kmemset(&ContextFrame, 0, sizeof(TRAP_FRAME));

    // IRET enters the loader as a synthetic C call target. SysV requires
    // RSP % 16 == 8 at function entry, while StackBase remains the true top.
    ContextFrame.rsp = (uint64_t)Thread->InternalThread.StackBase - 8;
    ContextFrame.rflags = USER_RFLAGS;
    ContextFrame.cs = USER_CS;
    ContextFrame.ss = USER_SS;
    Thread->InternalThread.TrapRegisters = ContextFrame;
    Thread->SystemThread = false;

    // Set state
    Thread->InternalThread.ThreadState = THREAD_READY;
    Thread->InternalThread.ApcState.SavedApcProcess = ParentProcess;

    // Get TEB.
    Status = MmCreateTeb(Thread, (void**) & Teb);
    if (MT_FAILURE(Status)) goto Cleanup;
    Thread->Teb = Teb;

    // Attach to process address space so we can bring in pageable memory.
    APC_STATE ApcState;
    MeAttachProcess(&ParentProcess->InternalProcess, &ApcState);

    // Write some basic information to TEB.
    try {
        Teb->UniqueThreadId = Thread->TID;
        Teb->UniqueProcessId = ParentProcess->PID;
        Teb->MtTib.StackBase = Thread->InternalThread.StackBase;
        Teb->MtTib.StackLimit = (void*)(((uintptr_t)Thread->InternalThread.StackBase - StackSize));
        Teb->MtTib.ExceptionList = MT_EXCEPTION_CHAIN_END;
        Status = MT_SUCCESS;
    } except{
        // Page fault on user addr.
        Status = GetExceptionCode();
    } end_try;

    // Detach
    MeDetachProcess(&ApcState);

    if (MT_FAILURE(Status)) goto Cleanup;

    // Set process's thread properties.
    if (IsMainThread) {

        // Main thread means ThreadParameter is actually the 3rd argument to LdrInitializeProcess in mtdll
        // RDI - Pointer to PEB.
        // RSI - Pointer to TEB.
        // RDX - Thread entry point.
        // RCX - Pointer to MTDLL_BASIC_TYPES.
        PTRAP_FRAME Trap = &Thread->InternalThread.TrapRegisters;
        Trap->rdi = (uint64_t)ParentProcess->Peb;
        Trap->rsi = (uint64_t)Teb;
        Trap->rdx = (uint64_t)EntryPoint;
        Trap->rcx = (uint64_t)ThreadParameter;
        Trap->rip = (uint64_t)MtdllEntrypoint;
    }
    else {
        // This is a process new thread, set execution entrypoint to LdrInitializeThread.
        // The process stores the trusted MTDLL mapping base in EPROCESS. Do
        // not walk its user-writable PEB loader list to choose a kernel-created
        // thread's initial instruction pointer.
        uintptr_t LdrInitializeThreadAddress = PspFindMtdllEntryAddress(
            MTDLL_THREAD_ROUTINE,
            Thread
        );

        if (!LdrInitializeThreadAddress) {
            Status = MT_NOT_FOUND;
            goto Cleanup;
        }

        // Address found, set it to RIP.
        PTRAP_FRAME Trap = &Thread->InternalThread.TrapRegisters;
        Trap->rip = (uint64_t)LdrInitializeThreadAddress;
        Trap->rdi = (uint64_t)Teb; // First argument, address of Teb in UM.
        Trap->rsi = (uint64_t)ParentProcess->Peb; // Second argument, address of Peb in UM.
        Trap->rdx = (uint64_t)EntryPoint; // Third argument, the thread's entrypoint.
        Trap->rcx = (uint64_t)ThreadParameter; // Fourth argument, the thread's parameter.
    }

    // Create a handle for the thread (and place it in the process's handle table).
    Status = ObCreateHandleForObjectEx(Thread, MT_THREAD_ALL_ACCESS, ThreadHandle, ParentProcess->ObjectTable);
    if (MT_FAILURE(Status)) goto Cleanup;

    ThreadHandleCreated = true;
    
    // Add to list of all threads in the parent process. (acquire its push lock)
    MsAcquirePushLockExclusive(&ParentProcess->ThreadListLock);
    if ((ParentProcess->Flags & (ProcessBeingTerminated | ProcessBeingDeleted)) ||
        (IsMainThread && ParentProcess->MainThread != NULL) ||
        (!IsMainThread && ParentProcess->MainThread == NULL)) {
        MsReleasePushLockExclusive(&ParentProcess->ThreadListLock);
        Status = (ParentProcess->Flags &
            (ProcessBeingTerminated | ProcessBeingDeleted))
            ? MT_PROCESS_IS_TERMINATING
            : (IsMainThread ? MT_INVALID_PARAM : MT_PROCESS_IS_TERMINATING);
        goto Cleanup;
    }
    if (IsMainThread) ParentProcess->MainThread = Thread;
    InsertTailList(&ParentProcess->AllThreads, &Thread->ThreadListEntry);
    if (ParentProcess->NumThreads == UINT32_MAX) {
        MeBugCheckEx(MEMORY_OVERFLOW_DETECTION, ParentProcess,
            Thread, &ParentProcess->AllThreads, RETADDR(0));
    }
    ParentProcess->NumThreads++;
    MsReleasePushLockExclusive(&ParentProcess->ThreadListLock);

    // Successful.
    Status = MT_SUCCESS;

    // Insert thread to processor queue.
    MeEnqueueThreadWithLock(&MeGetCurrentProcessor()->readyQueue, Thread);

Cleanup:
    if (RundownAcquired) {
        MsReleaseRundownProtection(&ParentProcess->ProcessRundown);
    }

    if (MT_FAILURE(Status)) {
        if (ThreadHandleCreated) {
            HtCloseEx(ParentProcess->ObjectTable, *ThreadHandle);
            *ThreadHandle = MT_INVALID_HANDLE;
        }
        if (Thread) ObDereferenceObject(Thread);
        if (ParentReferenced && !ParentReferenceTransferred) {
            ObDereferenceObject(ParentProcess);
        }
    }
    return Status;
}

MTSTATUS PsCreateSystemThread(ThreadEntry entry, THREAD_PARAMETER parameter, TimeSliceTicks TIMESLICE, _Out_Opt PETHREAD* OutThread)

/*++

    Routine description:

        Creates a kernel thread in the system process.

    Arguments:

        [IN] entry - List, table, or object entry affected by the routine.
        [IN] parameter - Context passed to the callback or worker.
        [IN] TIMESLICE - Scheduler quantum assigned to the thread.
        [OUT] OutThread - Receives the created thread object.

    Return Values:

        MT_SUCCESS on success, or an error status describing the failure.

--*/

{
    if (OutThread) *OutThread = NULL;
    if (unlikely(!PsInitialSystemProcess.PID)) return MT_NOT_FOUND; // The system process, somehow, hasn't been setupped yet.
    if (!entry || !TIMESLICE) return MT_INVALID_PARAM;

    // First, allocate a new thread. (using our shiny and glossy new object manager!!!)
    MTSTATUS Status;
    PETHREAD thread = NULL;
    Status = ObCreateObject(PsThreadType, sizeof(ETHREAD), (void**)&thread);
    if (MT_FAILURE(Status)) {
        return Status;
    }

    // Use unified helper.
    PspInitializeThread(thread, &PsInitialSystemProcess, TIMESLICE);
    thread->SystemThread = true;

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

    // saved rsp must point to the top (aligned), not sp-8
    cfm->rsp = (uint64_t)StackTop;
    cfm->rip = (uint64_t)ThreadWrapperEx;
    cfm->rdi = (uint64_t)entry; // first argument to ThreadWrapperEx (the entry point)
    cfm->rsi = (uint64_t)parameter; // second arugment to ThreadWrapperEx (the parameter pointer)

    cfm->ss = KERNEL_SS;
    cfm->cs = KERNEL_CS;

    // Canonical kernel RFLAGS: reserved bit 1 plus IF.
    cfm->rflags = INITIAL_RFLAGS;

    // Set it's registers and others.
    thread->InternalThread.TrapRegisters = *cfm;
    thread->TID = PsAllocateThreadId(thread);
    if (thread->TID == MT_INVALID_HANDLE) {
        ObDereferenceObject(thread);
        return MT_INVALID_HANDLE;
    }

    // Process stuffz
    thread->ParentProcess = &PsInitialSystemProcess; // The parent process for the system thread, is the system process.
    // Use the push lock to insert it into AllThreads.
    MsAcquirePushLockExclusive(&PsInitialSystemProcess.ThreadListLock);

    InsertTailList(&PsInitialSystemProcess.AllThreads, &thread->ThreadListEntry);
    if (PsInitialSystemProcess.NumThreads == UINT32_MAX) {
        MeBugCheckEx(MEMORY_OVERFLOW_DETECTION, &PsInitialSystemProcess,
            thread, &PsInitialSystemProcess.AllThreads, RETADDR(0));
    }
    PsInitialSystemProcess.NumThreads++;
    
    MsReleasePushLockExclusive(&PsInitialSystemProcess.ThreadListLock);

    // Enqueue it into processor
    MeEnqueueThreadWithLock(&MeGetCurrentProcessor()->readyQueue, thread);

    if (OutThread) {
        // The caller wants a pointer to the thread
        // He must gurantee to dereference the pointer when he has no use for it anymore
        // Else, when the thread terminates, there will be a pointer leak, and so the object will be kept alive for nothing.
        ObReferenceObject(thread);
        *OutThread = thread;
    }

    return MT_SUCCESS;
}

PETHREAD 
PsGetCurrentThread (void)

/*++

    Routine description:

        Returns the thread currently running on this processor.

    Arguments:

        None.

    Return Values:

        The thread currently running on this processor.

--*/

{
    return CONTAINING_RECORD(MeGetCurrentThread(), ETHREAD, InternalThread);
}

static
void
PspWakeThreadForTermination(
    IN PETHREAD Thread
)

/*++

    Routine description:

        Makes a blocked or sleeping thread runnable so it can process termination.

    Arguments:

        [IN] Thread - Thread affected by the operation.

    Return Values:

        None.

--*/

{
    PITHREAD IThread = &Thread->InternalThread;

    if (!MsClaimThreadWait(IThread, MT_SUCCESS)) {
        return;
    }

    WAIT_REASON WaitReason = IThread->WaitBlock.WaitReason;
    PDISPATCHER_HEADER WaitObject = IThread->WaitBlock.Object;

    // Winning WaitStatus transfers wake ownership from the timer/event path
    // to us. Remove both possible wait registrations before making it ready.
    MsRemoveTimerQueue(IThread);

    if (WaitReason == WaitReasonDispatcherObject) {
        IRQL DispatcherIrql;
        MsAcquireSpinlock(&WaitObject->Lock, &DispatcherIrql);

        // Recheck after spinlock acquirement
        if (IThread->WaitBlock.WaitReason != WaitReasonDispatcherObject ||
            IThread->WaitBlock.Object != WaitObject) {
            MeBugCheckEx(
                WAIT_STATE_FAILURE,
                IThread,
                (void*)(uintptr_t)IThread->WaitBlock.WaitReason,
                IThread->WaitBlock.Object,
                WaitObject
            );
        }

        // Link out the thread from the object list entry
        PDOUBLY_LINKED_LIST Entry = &IThread->WaitBlock.ObjectListEntry;

        if (!IsListEmpty(Entry)) {
            // If the list isn't empty, unlink
            RemoveEntryList(Entry);
            InitializeListHead(Entry);
        }
        
        MsReleaseSpinlock(&WaitObject->Lock, DispatcherIrql);
    }
    else if (WaitReason == WaitReasonSleep) {
        if (WaitObject != NULL) {
            MeBugCheckEx(
                WAIT_STATE_FAILURE,
                IThread,
                (void*)(uintptr_t)WaitReason,
                WaitObject,
                (void*)PspWakeThreadForTermination
            );
        }
    }
    else {
        MeBugCheckEx(
            WAIT_STATE_FAILURE,
            IThread,
            (void*)(uintptr_t)WaitReason,
            WaitObject,
            (void*)PspWakeThreadForTermination
        );
    }

    MeClearWaitBlock(IThread);
    MsCompleteThreadWait(IThread);
}

MTSTATUS
PsTerminateThread(
    IN PETHREAD Thread,
    IN MTSTATUS ExitStatus
)

/*++

    Routine description:

        Requests termination of a thread or exits the calling thread immediately.

    Arguments:

        [IN] Thread - Thread affected by the operation.
        [IN] ExitStatus - Termination status to record.

    Return Values:

        MT_SUCCESS on success, or an error status describing the failure.

--*/

{
#ifdef DEBUG
    gop_printf(COLOR_PINK, "**Terminating Thread TID %d (user mode: %d) for ExitStatus %x**\n", Thread->TID, !Thread->SystemThread, ExitStatus);
#endif

    // Or if it is the current thread, we just terminate ourselves.
    if (Thread == PsGetCurrentThread()) {
        // Exit current thread.
        PspExitThread(ExitStatus);
        return MT_SUCCESS;
    }

    // Do not terminate system threads
    if (Thread->SystemThread) {
        return MT_ACCESS_DENIED;
    }

    PITHREAD IThread = &Thread->InternalThread;

    // Allocate the termination APC.
    PAPC ExitApc = (PAPC)MmAllocatePoolWithTag(
        NonPagedPool,
        sizeof(APC),
        ' CPA'
    );
    if (!ExitApc) {
        return MT_NO_MEMORY;
    }

    if (InterlockedCompareExchangeU32(
        &Thread->TerminationState,
        ThreadTerminationInstalling,
        ThreadTerminationNone
    ) != ThreadTerminationNone) {
        MmFreePool(ExitApc);
        return MT_NOTHING_TO_TERMINATE;
    }

    // Flush ordinary APCs only after termination ownership is exclusive.
    PspRundownThreadApcs(IThread);

    // Queue termination as a user APC. Its kernel routine executes only when
    // the target has a complete CPL3 return frame, after the interrupted
    // syscall has released transient object references and other resources.
    MeInitializeApc(
        ExitApc,
        &Thread->InternalThread,
        UserMode,
        PspThreadTerminationRoutine,
        PspThreadRundown,
        NULL,
        NULL
    );

    // Publish the APC and the queued state under the same lock used by APC
    // retirement, then request delivery after releasing it.
    // This bypasses MeInsertQueueApc because this path already owns the thread's
    // termination state and ordinary APC insertion is now intentionally closed.
    PPROCESSOR TargetProcessor = NULL;
    IRQL OldIrql;
    MsAcquireSpinlock(&IThread->ApcQueueLock, &OldIrql);

    ExitApc->SystemArgument1 = (void*)(uintptr_t)ExitStatus;
    ExitApc->SystemArgument2 = NULL;
    ExitApc->Inserted = 1;
    InsertTailList(
        &IThread->ApcState.ApcListHead[UserMode],
        &ExitApc->ApcListEntry
    );
    InterlockedStoreRelease(
        &IThread->ApcState.UserApcPending,
        true
    );
    InterlockedStoreRelease(
        &Thread->TerminationState,
        ThreadTerminationQueued
    );
    if (IThread->ThreadState == THREAD_RUNNING &&
        IThread->ActiveProcessor != NULL) {
        TargetProcessor = IThread->ActiveProcessor;
    }
    MsReleaseSpinlock(&IThread->ApcQueueLock, OldIrql);

    if (TargetProcessor) {
        PPROCESSOR CurrentProcessor = MeGetCurrentProcessor();
        if (TargetProcessor == CurrentProcessor) {
            bool InterruptsEnabled = MeDisableInterrupts();
            if (CurrentProcessor->currentThread == IThread) {
                MhRequestSoftwareInterrupt(APC_LEVEL);
            }
            MeEnableInterrupts(InterruptsEnabled);
        }
        else {
            IPI_PARAMS IpiParams = { 0 };
            MhSendActionToSpecificCpuAndWait(
                TargetProcessor,
                CPU_ACTION_REQUEST_APC,
                IpiParams
            );
        }
    }

    // A kernel APC cannot run while its target is blocked forever. Claim its
    // wait and make it runnable; normal event/timer wakes use the same CAS.
    PspWakeThreadForTermination(Thread);

    return MT_SUCCESS;
}

void
PsDeleteThread(
    IN void* Object
)

/*++

    Routine description:

        Releases the final resources owned by a deleted thread object.

    Arguments:

        [IN OUT] Object - Object affected by the operation.

    Return Values:

        None.

--*/

{
    // This function is called when the reference count for this thread has reached 0 (e.g, it is no longer in use)
    // (it is called after thread termination)
    // We free everything that the ETHREAD uses.
    PETHREAD Thread = (PETHREAD)Object;

    bool IsKernelThread = PsIsKernelThread(Thread);

    // Keep ThreadListEntry linked throughout thread exit. Process thread-list
    // iterators hold an object reference to keep their cursor alive, so final
    // object deletion is the point at which the entry can safely be removed.
    // 
    // Thread is Thread B.
    // 
    // Turns from: Thread A <-> Thread B <-> Thread C
    //
    // to: Thread A <-> Thread C
    //
    PEPROCESS ParentProcess = Thread->ParentProcess;
    if (ParentProcess) {
        MsAcquirePushLockExclusive(&ParentProcess->ThreadListLock);

        PDOUBLY_LINKED_LIST Entry = &Thread->ThreadListEntry;
        if (Entry->Flink != NULL && Entry->Blink != NULL &&
            Entry->Flink != Entry && Entry->Blink != Entry) {
            RemoveEntryList(Entry);
            InitializeListHead(Entry);
        }

        if (ParentProcess->MainThread == Thread) {
            ParentProcess->MainThread = NULL;
        }

        MsReleasePushLockExclusive(&ParentProcess->ThreadListLock);
    }

    // Free TID only if construction reached CID allocation.
    if (Thread->TID > 0 && Thread->TID != MT_INVALID_HANDLE) {
        PsFreeCid(Thread->TID);
    }

    // Free its stack.
    if (Thread->InternalThread.KernelStack) {
        PsDeferKernelStackDeletion(Thread->InternalThread.KernelStack, Thread->InternalThread.IsLargeStack);
        Thread->InternalThread.KernelStack = NULL;
    }

    if (!IsKernelThread && Thread->ParentProcess) {
        // Release per-thread user allocations while the parent reference still
        // guarantees that its address-space bookkeeping exists.
        if (Thread->Teb) {
            void* TebBase = Thread->Teb;
            size_t RegionSize = 0;
            MmFreeVirtualMemory(Thread->ParentProcess, &TebBase, &RegionSize, MEM_RELEASE);
            Thread->Teb = NULL;
        }

        uintptr_t StackTop = (uintptr_t)Thread->InternalThread.StackBase;
        size_t StackSize = Thread->UserStackSize;
        if (StackSize != 0 && StackTop >= VirtualPageSize &&
            StackSize <= StackTop - VirtualPageSize) {
            void* StackBase = (void*)(StackTop - StackSize);
            void* GuardBase = (void*)((uintptr_t)StackBase - VirtualPageSize);
            size_t RegionSize = 0;
            MmFreeVirtualMemory(Thread->ParentProcess, &GuardBase, &RegionSize, MEM_RELEASE);
            RegionSize = 0;
            MmFreeVirtualMemory(Thread->ParentProcess, &StackBase, &RegionSize, MEM_RELEASE);
            Thread->InternalThread.StackBase = NULL;
            Thread->UserStackSize = 0;
        }

        ObDereferenceObject(Thread->ParentProcess);
        Thread->ParentProcess = NULL;
    }

    // When we reach here, the function returns, and the ETHREAD is deleted.
}

NORETURN
void
PspExitThread(
    IN MTSTATUS ExitStatus
)

/*++

    Routine description:

        Completes teardown of the current thread and transfers control to the scheduler.

    Arguments:

        [IN] ExitStatus - Termination status to record.

    Return Values:

        None.

--*/

{
    // This exits the current running thread on the processor.
    PETHREAD Thread = PsGetCurrentThread();
    PEPROCESS CurrentProcess = Thread->InternalThread.ApcState.SavedApcProcess;

    PspBeginThreadExit(Thread);

#ifdef DEBUG
    HANDLE MainThreadId = CurrentProcess->MainThread
        ? CurrentProcess->MainThread->TID
        : (HANDLE)-1;
    gop_printf(COLOR_WHITE, "**PspExitThread called on thread tid %d (parent name %s) (parent main thread %d) for status %x (MTSTATUS)**\n", Thread->TID, CurrentProcess->ImageName, MainThreadId, ExitStatus);
#endif

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

    // Wait for rundown protection release
    MsWaitForRundownProtectionRelease(&Thread->ThreadRundown);

    // Acquire process lock before we modify thread entries.
    MsAcquirePushLockExclusive(&CurrentProcess->ThreadListLock);

    // Verify that the active thread is still represented in the process list.
    // The entry remains linked until PsDeleteThread so referenced iterators can
    // safely use it as their cursor after this thread has exited.
    PDOUBLY_LINKED_LIST listHead = &CurrentProcess->AllThreads;
    PDOUBLY_LINKED_LIST entry = listHead->Flink;
    bool ThreadFound = false;

    while (entry != listHead) {
        PETHREAD iter = CONTAINING_RECORD(entry, ETHREAD, ThreadListEntry);
        if (iter == Thread) {
            ThreadFound = true;
            break;
        }
        entry = entry->Flink;
    }

    if (!ThreadFound || CurrentProcess->NumThreads == 0) {
        MeBugCheckEx(
            SCHEDULER_FAILURE,
            Thread,
            CurrentProcess,
            (void*)(uintptr_t)CurrentProcess->NumThreads,
            RETADDR(0)
        );
    }

    // NumThreads counts live threads, while AllThreads may retain exited thread
    // objects until their final references are released.
    CurrentProcess->NumThreads--;
    bool LastThread = CurrentProcess->NumThreads == 0;
    if (LastThread) {
        InterlockedOr32(
            (volatile int32_t*)&CurrentProcess->Flags,
            ProcessBeingTerminated
        );
    }

    if (CurrentProcess->MainThread == Thread) {
        CurrentProcess->MainThread = NULL;
        for (entry = listHead->Flink; entry != listHead; entry = entry->Flink) {
            PETHREAD Candidate = CONTAINING_RECORD(
                entry,
                ETHREAD,
                ThreadListEntry
            );
            if (Candidate != Thread &&
                InterlockedLoadAcquire(
                    &Candidate->TerminationState
                ) != ThreadTerminationExiting) {
                CurrentProcess->MainThread = Candidate;
                break;
            }
        }
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
        MsWaitForRundownProtectionRelease(&CurrentProcess->ProcessRundown);

        // This is the last thread of the process, we clear its handle table.
        HtDeleteHandleTable(CurrentProcess->ObjectTable);
        CurrentProcess->ObjectTable = NULL;

        // This is where the process terminates.
        // Acquire the process dispatcher header lock.
        IRQL prevIrql;
        MsAcquireSpinlock(&CurrentProcess->InternalProcess.Header.Lock, &prevIrql);

        // Publish the final state and status under the same lock as the
        // persistent process signal. A resumed waiter must not observe
        // PROCESS_TERMINATING after termination completed.
        CurrentProcess->InternalProcess.ProcessState = PROCESS_TERMINATED;
        CurrentProcess->ExitStatus = ExitStatus;
        CurrentProcess->InternalProcess.Header.SignalState = 1;

        // Wake up everyone!
        PITHREAD WaitingThread = MspDequeueNextWaitThreadLocked(&CurrentProcess->InternalProcess.Header.WaitListHead);

        while (WaitingThread != NULL) {
            // Try to claim the thread for a successful wake
            if (MsClaimThreadWait(WaitingThread, MT_SUCCESS)) {
                // Completion can acquire scheduler locks, so finish it outside
                // the process dispatcher's lock.
                MsReleaseSpinlock(&CurrentProcess->InternalProcess.Header.Lock, prevIrql);

                MsRemoveTimerQueue(WaitingThread);
                MsCompleteThreadWait(WaitingThread);

                MsAcquireSpinlock(&CurrentProcess->InternalProcess.Header.Lock, &prevIrql);
            }

            // We failed to claim the thread, retry with next waiter if any
            WaitingThread = MspDequeueNextWaitThreadLocked(&CurrentProcess->InternalProcess.Header.WaitListHead);
        }

        MsReleaseSpinlock(&CurrentProcess->InternalProcess.Header.Lock, prevIrql);
    }

    // TODO termination ports for a process (so when it dies the user process can like show a message to parent process)

    // APC rundown was completed before this terminal teardown path began.

    // Abandon every mutex still owned by this thread. Remove one list entry
    // under the owner-list lock, release it, then process that mutex separately.
    while (1) {
        IRQL prevIrqlion;
        MsAcquireSpinlock(&Thread->InternalThread.OwnedMutexesListLock, &prevIrqlion);

        // Remove one owned-mutex entry from the dying thread's list.
        PDOUBLY_LINKED_LIST Head = RemoveHeadList(&Thread->InternalThread.OwnedMutexListHead);

        // Restore a removed mutex entry to its self-linked state before the
        // mutex is either handed off or left available and abandoned.
        if (Head != NULL) {
            InitializeListHead(Head);
        }

        // Release the lock.
        MsReleaseSpinlock(&Thread->InternalThread.OwnedMutexesListLock, prevIrqlion);

        if (Head == NULL) {
            break;
        }

        // Recover the mutex containing this owner-list entry.
        PMUTEX Mutex = CONTAINING_RECORD(Head, MUTEX, OwnerListEntry);

        // Now acquire the Mutex dispatcher lock
        MsAcquireSpinlock(&Mutex->Header.Lock, &prevIrqlion);

        // The mutex must still name the dying thread because its owner-list
        // entry was removed before acquiring this dispatcher lock.
        assert(Mutex->OwnerThread == Thread);

        // User-visible mutex acquisitions retain one object reference per
        // recursion level. Ownership is ending here, so detach the dead
        // owner's references while the mutex state is still locked. A waiter
        // receiving ownership retains its own reference when its wait returns.
        uint32_t ObjectOwnerReferences = Mutex->ObjectOwnerReferences;
        Mutex->ObjectOwnerReferences = 0;

        // Track whether abandonment was transferred directly to a waiter.
        bool WaiterFound = false;

        // Find the first waiter whose pending wait can still be claimed.
        PITHREAD WaitingThread = MspDequeueNextWaitThreadLocked(&Mutex->Header.WaitListHead);

        while (WaitingThread != NULL) {
            if (MsClaimThreadWait(WaitingThread, MT_MUTEX_ABANDONED)) {
                WaiterFound = true;
                // Transfer ownership directly to the claimed waiting thread.
                Mutex->Abandoned = false;
                Mutex->OwnerThread = PsGetEThreadFromIThread(WaitingThread);
                Mutex->Header.SignalState = 0; // Mutex held by owner thread
                
                // With the mutex lock held, take the replacement owner's list
                // lock and link the mutex using the documented lock order.
                MsAcquireSpinlockAtDpcLevel(&WaitingThread->OwnedMutexesListLock);
                InsertTailList(&WaitingThread->OwnedMutexListHead, &Mutex->OwnerListEntry);
                MsReleaseSpinlockFromDpcLevel(&WaitingThread->OwnedMutexesListLock);

                // Drop the mutex lock before timer removal and wait completion.
                MsReleaseSpinlock(&Mutex->Header.Lock, prevIrqlion);
                MsRemoveTimerQueue(WaitingThread);
                MsCompleteThreadWait(WaitingThread);

                // Reacquire so the common loop cleanup releases one held lock.
                MsAcquireSpinlock(&Mutex->Header.Lock, &prevIrqlion);
                break;
            }

            WaitingThread = MspDequeueNextWaitThreadLocked(&Mutex->Header.WaitListHead);
        }

        if (!WaiterFound) {
            // No waiter won. Leave the mutex available but marked abandoned so
            // the next immediate acquirer receives MT_MUTEX_ABANDONED.
            Mutex->Abandoned = true;
            Mutex->OwnerThread = NULL;
            Mutex->Header.SignalState = 1;
        }

        // Release the lock for the next loop.
        MsReleaseSpinlock(&Mutex->Header.Lock, prevIrqlion);

        while (ObjectOwnerReferences != 0) {
            ObDereferenceObject(Mutex);
            ObjectOwnerReferences--;
        }
    }

    assert(IsListEmpty(&Thread->InternalThread.OwnedMutexListHead));

    // Finally, terminate this thread from the scheduler.
    MeDisableInterrupts();
    Thread->InternalThread.ThreadState = THREAD_TERMINATING;

    // Acquire the thread dispatcher header lock.
    IRQL prevIrql;
    MsAcquireSpinlock(&Thread->InternalThread.Header.Lock, &prevIrql);

    // Publish the final status under the same lock as the persistent thread
    // signal. Querying the exit code and waiting for termination therefore
    // observe one atomic terminal transition.
    Thread->ExitStatus = ExitStatus;
    Thread->InternalThread.Header.SignalState = 1;

    // Wake up everyone that are waiting on this thread's termination.
    PITHREAD WaitingThread = MspDequeueNextWaitThreadLocked(&Thread->InternalThread.Header.WaitListHead);

    while (WaitingThread != NULL) {
        // Try to claim the thread for a successful wake
        if (MsClaimThreadWait(WaitingThread, MT_SUCCESS)) {
            // Completion can acquire scheduler locks, so finish it outside
            // the thread dispatcher's lock.
            MsReleaseSpinlock(&Thread->InternalThread.Header.Lock, prevIrql);

            MsRemoveTimerQueue(WaitingThread);
            MsCompleteThreadWait(WaitingThread);

            MsAcquireSpinlock(&Thread->InternalThread.Header.Lock, &prevIrql);
        }

        // We failed to claim the thread, retry with next waiter if any
        WaitingThread = MspDequeueNextWaitThreadLocked(&Thread->InternalThread.Header.WaitListHead);
    }

    MsReleaseSpinlock(&Thread->InternalThread.Header.Lock, prevIrql);

    // Schedule away.
    Schedule();
}
