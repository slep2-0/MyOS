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
#define MAX_LOADER_LIST_ENTRIES 1024u

// Clean exit for a thread—never returns!
static void ThreadExit(void) {
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
static void ThreadWrapperEx(ThreadEntry thread_entry, THREAD_PARAMETER parameter) {
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

{
    // Free it.
    MmFreePool(Apc);
}

static void
PspRundownThreadApcs(
    IN PITHREAD Thread
)
{
    for (;;) {
        IRQL OldIrql;
        MsAcquireSpinlock(&Thread->ApcQueueLock, &OldIrql);

        PDOUBLY_LINKED_LIST Entry = RemoveHeadList(&Thread->ApcListHead);
        if (!Entry) {
            MsReleaseSpinlock(&Thread->ApcQueueLock, OldIrql);
            return;
        }

        PAPC Apc = CONTAINING_RECORD(Entry, APC, ApcListEntry);
        Apc->Inserted = 0;
        MsReleaseSpinlock(&Thread->ApcQueueLock, OldIrql);

        if (Apc->RundownRoutine) {
            Apc->RundownRoutine(Apc);
        }
    }
}

static void
PspBeginThreadExit(
    IN PETHREAD Thread
)
{
    uint32_t State = __atomic_load_n(
        &Thread->TerminationState,
        __ATOMIC_ACQUIRE
    );

    for (;;) {
        // The installing CPU publishes the APC while holding ApcQueueLock and
        // changes this state before it can request delivery.
        if (State == ThreadTerminationInstalling) {
            __pause();
            State = __atomic_load_n(
                &Thread->TerminationState,
                __ATOMIC_ACQUIRE
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

{
    UNREFERENCED_PARAMETER(Apc); UNREFERENCED_PARAMETER(NormalContext); UNREFERENCED_PARAMETER(NormalRoutine); UNREFERENCED_PARAMETER(SystemArgument1); UNREFERENCED_PARAMETER(SystemArgument2);
    // Call PspExitThread, we are terminating this thread.
    Apc->RundownRoutine(Apc);

    // Set APC Routine as non active in the current CPU, as PspExitThread is a NORETURN
    MeGetCurrentProcessor()->ApcRoutineActive = false;

    PspExitThread((MTSTATUS)(uintptr_t)*SystemArgument1);
}


void 
PspInitializeThread(
    PETHREAD Thread, PEPROCESS Process, TimeSliceTicks TimeSlice
)

{
    // Basic linking
    InitializeListHead(&Thread->ThreadListEntry);
    InitializeListHead(&Thread->InternalThread.ApcListHead);
    Thread->InternalThread.UserApcActive = false;
    Thread->TerminationState = ThreadTerminationNone;

    // Process association
    Thread->ParentProcess = Process;
    Thread->InternalThread.ApcState.SavedApcProcess = Process;
    Thread->PID = Process->PID;

    // Scheduling defaults
    Thread->InternalThread.TimeSlice = TimeSlice;
    Thread->InternalThread.TimeSliceAllocated = TimeSlice;
    Thread->InternalThread.ThreadState = THREAD_READY;
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

    if (__atomic_load_n(&ParentProcess->Flags, __ATOMIC_ACQUIRE) &
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
        // Since a thread is already created, this means the entries are already cached in memory
        // so a file object isnt require for PspFindMtdllEntryRva
        void* LdrInitializeThreadRva = PspFindMtdllEntryRva(NULL, MTDLL_THREAD_ROUTINE);

        if (!LdrInitializeThreadRva) {
            Status = MT_NOT_FOUND;
            goto Cleanup;
        }

        uintptr_t LdrInitializeThreadAddress = 0;
        
        // Try to find MTDLL base in the target address space. This matters for
        // CreateRemoteThread: its PEB is not mapped by the caller's CR3.
        APC_STATE LoaderApcState;
        kmemset(&LoaderApcState, 0, sizeof(LoaderApcState));
        MeAttachProcess(&ParentProcess->InternalProcess, &LoaderApcState);

        try {
            PPEB Peb = ParentProcess->Peb;

            PDOUBLY_LINKED_LIST Head = &Peb->LoaderData.LoadedModuleList;
            PDOUBLY_LINKED_LIST Current = Peb->LoaderData.LoadedModuleList.Flink;
            uint32_t EntriesVisited = 0;

            while (Head != Current && EntriesVisited++ < MAX_LOADER_LIST_ENTRIES) {
                if (!MI_IS_CANONICAL_ADDR(Current) ||
                    (uintptr_t)Current > MmHighestUserAddress) {
                    Status = MT_INVALID_ADDRESS;
                    break;
                }

                LDR_DATA_TABLE_ENTRY* Entry = CONTAINING_RECORD(Current, LDR_DATA_TABLE_ENTRY, LoadedModuleList);

                if (kstrncmp(Entry->FullName, MTDLL_PATH, sizeof(MTDLL_PATH)) == 0) {
                    uintptr_t MtdllBase = (uintptr_t)Entry->Base;
                    uintptr_t RoutineRva = (uintptr_t)LdrInitializeThreadRva;
                    if (MtdllBase <= MmHighestUserAddress &&
                        RoutineRva <= MmHighestUserAddress - MtdllBase) {
                        LdrInitializeThreadAddress = MtdllBase + RoutineRva;
                    }
                    break;
                }

                Current = Current->Flink;
            }

            if (Head != Current &&
                EntriesVisited >= MAX_LOADER_LIST_ENTRIES &&
                !LdrInitializeThreadAddress) {
                Status = MT_INVALID_STATE;
            }

        } except{
            Status = GetExceptionCode();
        }
        end_try;

        MeDetachProcess(&LoaderApcState);

        if (MT_FAILURE(Status)) goto Cleanup;

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

    // Initialize APC List head.
    InitializeListHead(&Thread->InternalThread.ApcListHead);

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

MTSTATUS PsCreateSystemThread(ThreadEntry entry, THREAD_PARAMETER parameter, TimeSliceTicks TIMESLICE, _Out_Opt PETHREAD* OutThread) {
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

    // Enqueue it into processor. TODO START SUSPENDED?
    MeEnqueueThreadWithLock(&MeGetCurrentProcessor()->readyQueue, thread);
    if (OutThread) *OutThread = thread;
    return MT_SUCCESS;
}

PETHREAD 
PsGetCurrentThread (void) {
    return CONTAINING_RECORD(MeGetCurrentThread(), ETHREAD, InternalThread);
}

static
void
PspWakeThreadForTermination(
    IN PETHREAD Thread
)
{
    PITHREAD IThread = &Thread->InternalThread;

    if (!MsClaimThreadWait(IThread, MT_SUCCESS)) {
        return;
    }

    // Winning WaitStatus transfers wake ownership from the timer/event path
    // to us. Remove both possible wait registrations before making it ready.
    MsRemoveTimerQueue(IThread);

    PEVENT Event = Thread->CurrentEvent;
    if (Event) {
        IRQL EventIrql;
        MsAcquireSpinlock(&Event->lock, &EventIrql);
        if (Thread->CurrentEvent == Event) {
            MeRemoveThreadFromQueue(&Event->waitingQueue, Thread);
            Thread->CurrentEvent = NULL;
        }
        MsReleaseSpinlock(&Event->lock, EventIrql);
    }

    MsCompleteThreadWait(IThread);
}

MTSTATUS
PsTerminateThread(
    IN PETHREAD Thread,
    IN MTSTATUS ExitStatus
)

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

    // Initialize it.
    MeInitializeApc(
        ExitApc,
        &Thread->InternalThread,
        KernelMode,
        PspThreadTerminationRoutine,
        PspThreadRundown,
        NULL,
        NULL
    );

    // Publish the APC and the queued state under the same lock used by APC
    // retirement, then request delivery after releasing it.
    PPROCESSOR TargetProcessor = NULL;
    IRQL OldIrql;
    MsAcquireSpinlock(&IThread->ApcQueueLock, &OldIrql);
    ExitApc->SystemArgument1 = (void*)(uintptr_t)ExitStatus;
    ExitApc->SystemArgument2 = NULL;
    ExitApc->Inserted = 1;
    InsertTailList(&IThread->ApcListHead, &ExitApc->ApcListEntry);
    __atomic_store_n(
        &Thread->TerminationState,
        ThreadTerminationQueued,
        __ATOMIC_RELEASE
    );
    if (IThread->ThreadState == THREAD_RUNNING &&
        IThread->ActiveProcessor != NULL) {
        TargetProcessor = IThread->ActiveProcessor;
    }
    MsReleaseSpinlock(&IThread->ApcQueueLock, OldIrql);

    if (TargetProcessor) {
        PPROCESSOR CurrentProcessor = MeGetCurrentProcessor();
        if (TargetProcessor == CurrentProcessor) {
            CurrentProcessor->ApcInterruptRequested = true;
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

{
    // This function is called when the reference count for this thread has reached 0 (e.g, it is no longer in use)
    // (it is called after thread termination)
    // We free everything that the ETHREAD uses.
    PETHREAD Thread = (PETHREAD)Object;

    bool IsKernelThread = PsIsKernelThread(Thread);

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

    // Remove us from the process thread list.
    PDOUBLY_LINKED_LIST listHead = &CurrentProcess->AllThreads;
    PDOUBLY_LINKED_LIST entry = listHead->Flink;
    bool ThreadFound = false;

    while (entry != listHead) {
        PETHREAD iter = CONTAINING_RECORD(entry, ETHREAD, ThreadListEntry);
        if (iter == Thread) {
            // Remove entry
            entry->Blink->Flink = entry->Flink;
            entry->Flink->Blink = entry->Blink;

            // Set entry to point at itself
            InitializeListHead(&Thread->ThreadListEntry);
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

    // No new thread may publish into a process once its final live thread has
    // left the list. In-flight creators are drained below before table teardown.
    CurrentProcess->NumThreads--;
    bool LastThread = CurrentProcess->NumThreads == 0;
    if (LastThread) {
        InterlockedOr32(
            (volatile int32_t*)&CurrentProcess->Flags,
            ProcessBeingTerminated
        );
        CurrentProcess->InternalProcess.ProcessState = PROCESS_TERMINATING;
        CurrentProcess->ExitStatus = ExitStatus;
    }

    if (CurrentProcess->MainThread == Thread) {
        CurrentProcess->MainThread = IsListEmpty(listHead)
            ? NULL
            : CONTAINING_RECORD(listHead->Flink, ETHREAD, ThreadListEntry);
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
