#ifndef X86_MATANEL_PROCESS_H
#define X86_MATANEL_PROCESS_H

/*++

Module Name:

    mp.h

Purpose:

    This module contains the header files required for process and thread management.

Author:

    slep (Matanel) 2025.

Revision History:

--*/

// Base includes
#include <stdint.h>
#include <stddef.h>

// Other file includes
#include "me.h"
#include "ht.h"
#include "ob.h"
#include "core.h"
#include "mt.h"
#include "../../shared/include/accessrights.h"

// Exception Includes
#include "exception.h"

// ------------------ ENUMERATORS ------------------

typedef enum _
 {
    THREAD_RUNNING,
    THREAD_READY,
    THREAD_BLOCKED,
    THREAD_TERMINATING,
    THREAD_TERMINATED,
    THREAD_ZOMBIE,
    // The current CPU is committing a wait but still owns the kernel stack.
    THREAD_BLOCKING,
    // Thread is only in its initialization stage, it has not run yet.
    THREAD_INITIALIZED,
} THREAD_STATE, *PTHREAD_STATE;

typedef enum _THREAD_TERMINATION_STATE {
    ThreadTerminationNone = 0,
    ThreadTerminationInstalling,
    ThreadTerminationQueued,
    ThreadTerminationExiting
} THREAD_TERMINATION_STATE;

typedef enum _PROCESS_STATE {
    PROCESS_RUNNING = 0, // A thread in the process is currently running
    PROCESS_READY,  // The process is ready to run. (essentially its threads)
    PROCESS_WAITING,  // Waiting on a mutual exclusion or just sleeping.
    PROCESS_TERMINATING,   // The process is ongoing termination in the kernel.
    PROCESS_TERMINATED,  // Process is terminated, but core parts of its structure has been kept.
    PROCESS_SUSPENDED // The process has been suspended by the kernel, either by choice or forcefully.
} PROCESS_STATE, *PPROCESS_STATE;

typedef enum _PS_PHASE_ROUTINE {
    PS_PHASE_INITIALIZE_SYSTEM = 0,
    PS_PHASE_INITIALIZE_WORKER_THREADS, 
} PS_PHASE_ROUTINE;

// ------------------ STRUCTURES ------------------

#define MTDLL_PATH "mtdll.mtdll" // root dir

typedef enum _PROCESS_FLAGS {
    ProcessBreakOnTermination = (1 << 0),
    ProcessBeingTerminated = (1 << 1),
    ProcessBeingDeleted = (1 << 2),
} PROCESS_FLAGS;

typedef struct _EPROCESS {
    struct _IPROCESS InternalProcess; // Internal process structure. (KPROCESS Equivalent-ish)
    char ImageName[24]; // Process image name - e.g "mtoskrnl.mtexe"
    HANDLE PID; // Process Identifier, unique identifier to the process. (do not use HtClose on this, only PsDeleteCid)
    HANDLE ParentProcessPid; // Parent process identifier captured at creation.
    THREAD_PRIORITY BasePriority; // Default base priority inherited by new threads.
    uint64_t CreationTime; // Timestamp of creation, seconds from 1970 January 1st. (may change)
    // SID TODO. - User info as well, when users.

    PPEB Peb; // Accessible only pageable IRQL (APC_LEVEL and below), and only when process is setupped.
    void* SectionObject; // MM_SECTION Object for the process section.
    void* MtdllSection; // MM_SECTION Object for MTDLL in the process section.
    void* MtdllBase; // Kernel-owned base of the process MTDLL mapping.

    // Synchorinzation for internal functions.
    struct _RUNDOWN_REF ProcessRundown; // A process rundown that is used to safely synchronize the teardown or deletion of a process, ensuring no pointer is still active & accessing it.
    struct _PUSH_LOCK ProcessLock; // A process push lock that is used to mutually synchronize access to its locked objects, allowing 1 writer at a time, but multiple readers (if no writers)

    // Thread infos
    struct _ETHREAD* MainThread; // Pointer to the main thread created for the process.
    PUSH_LOCK ThreadListLock; // Protects synchronization in AllThreads.
    DOUBLY_LINKED_LIST AllThreads; // Thread objects remain linked until final object deletion.
    uint32_t NumThreads; // Number of live threads; exited referenced objects may remain in AllThreads.
    PUSH_LOCK AddressSpaceLock; // A push lock designed to protect synchronization in creating the next stack for another thread in the PROCESS.
    uintptr_t NextStackHint; // Top down search for the next stack.

    // Handle Table
    PHANDLE_TABLE ObjectTable;

    // Special Flags.
    enum _PROCESS_FLAGS Flags;
    MTSTATUS ExitStatus;

    // VAD (todo process quota)
    struct _MMVAD* VadRoot; // The Root of the VAD for the process. (used to find free virtual addresses spaces in the process, and information about them)
    PUSH_LOCK VadLock; // The push lock to ensure VAD atomicity.
} EPROCESS, *PEPROCESS;

typedef struct _ETHREAD {
    struct _ITHREAD InternalThread; // Internal thread structure. (KTHREAD Equivalent-ish)
    PTEB Teb;
    size_t UserStackSize;
    HANDLE TID;           /* thread id */
    struct _EPROCESS* ParentProcess; /* pointer to the parent process of the thread */
    struct _DOUBLY_LINKED_LIST ThreadListEntry; // Forward and backward links to queue threads in.
    struct _DOUBLY_LINKED_LIST SchedulerListEntry; // Forward and backward links that the scheduler enqueues and dequeues threads from.
    struct _RUNDOWN_REF ThreadRundown; // A thread rundown that is used to safely synchronize the teardown or deletion of a thread, ensuring no other threads are still accessing it.
    PUSH_LOCK ThreadLock; // Used for mutual synchronization.
    MTSTATUS ExitStatus; // The status the thread exited in.
    volatile uint32_t TerminationState;

    // LastStatus != user mode TEB LastStatus
    MTSTATUS LastStatus; // The last status set by violation.
    bool SystemThread; // Is this thread a system thread?
    bool WorkerThread; // is this thread a worker thread?
    /* TODO: priority, affinity, wait list, etc. */
} ETHREAD, *PETHREAD;

typedef struct _STACK_REAPER_ENTRY {
    struct _STACK_REAPER_ENTRY* Next;
    void* StackBase;
    bool IsLarge;
} STACK_REAPER_ENTRY, * PSTACK_REAPER_ENTRY;

// ------------------ MACROS ------------------
#define PROCESS_STACK_SIZE (32*1024) // 32 KiB
#define PROCESS_STACK_ALIGNMENT 16 // Alignment of 16 Bytes.

// ------------------ TYPE DEFINES ------------------

typedef void* THREAD_PARAMETER;
typedef void (*ThreadEntry)(THREAD_PARAMETER);

// ------------------ FUNCTIONS ------------------

extern EPROCESS PsInitialSystemProcess;

struct _MT_CREATE_PROCESS_PARAMETERS;

MTSTATUS
PsCreateProcess(
    IN const struct _MT_CREATE_PROCESS_PARAMETERS* Parameters,
    OUT PMT_PROCESS_INFORMATION ProcessInformation,
    OUT PETHREAD* InitialThread
);


MTSTATUS
PsCreateThread(
    IN PEPROCESS Process,
    OUT PHANDLE ThreadHandle,
    IN THREAD_START_ROUTINE EntryPoint,
    IN THREAD_PARAMETER ThreadParameter,
    IN TimeSliceTicks TimeSlice,
    IN ThreadEntry MtdllEntrypoint,
    OUT PETHREAD* CreatedThread
);

void
PspAbortThreadCreation(
    IN PETHREAD Thread,
    IN MTSTATUS ExitStatus
);

#define MtYield() MsYieldExecution(&PsGetCurrentThread()->InternalThread.TrapRegisters);

extern void MsYieldExecution(PTRAP_FRAME threadRegisters);

// If OutThread is supplied, caller must dereference the thread after he is done with it.
MTSTATUS PsCreateSystemThread(ThreadEntry entry, THREAD_PARAMETER parameter, TimeSliceTicks TIMESLICE, _Out_Opt PETHREAD* OutThread);

MTSTATUS
PsInitializeSystem(
    IN enum _PS_PHASE_ROUTINE Phase
);

void PsDeferKernelStackDeletion(void* StackBase, bool IsLarge);

MTSTATUS
PsTerminateProcess(
    IN PEPROCESS Process,
    IN MTSTATUS ExitCode
);

void
PspInitializeThread(
    PETHREAD Thread, PEPROCESS Process, TimeSliceTicks TimeSlice
);

MTSTATUS
PsTerminateThread(
    IN PETHREAD Thread,
    IN MTSTATUS ExitStatus
);

NORETURN
void
PspExitThread(
    IN MTSTATUS ExitStatus
);

void
PsDeleteThread(
    IN void* Object
);

void
PsDeleteProcess(
    IN void* ProcessObject
);

PETHREAD
PsGetNextProcessThread(
    IN PEPROCESS Process,
    _In_Opt PETHREAD LastThread
);

PETHREAD
PsGetCurrentThread(
    void
);

void PsInitializeWorkerThreads(void);

void
PsInitializeCidTable(
    void
);

FORCEINLINE
PEPROCESS
PsGetCurrentProcess(
    void
)

// Will return the current process the thread is attached to (could be its parent thread, could be another in an APC)

{
    if (MeGetCurrentThread()) {
        return MeGetCurrentThread()->ApcState.SavedApcProcess;
    }
    else return NULL;
}

FORCEINLINE
PETHREAD
PsGetEThreadFromIThread(
    IN PITHREAD IThread
)

{
    return CONTAINING_RECORD(IThread, ETHREAD, InternalThread);
}

FORCEINLINE
PEPROCESS
PsGetEProcessFromIProcess(
    IN PIPROCESS IProcess
)

{
    return CONTAINING_RECORD(IProcess, EPROCESS, InternalProcess);
}

FORCEINLINE
bool
PsIsKernelThread(
    IN PETHREAD Thread
)

{
    return (Thread && Thread->SystemThread);
}

FORCEINLINE
MTSTATUS
GetExceptionCode(
    void
)

{
    PETHREAD CurrentThread = PsGetCurrentThread();
    if (CurrentThread) return CurrentThread->LastStatus;
    else return MT_SUCCESS; // Fallback
}

HANDLE
PsAllocateProcessId(
    IN  PEPROCESS Process
);

HANDLE
PsAllocateThreadId(
    IN  PETHREAD Thread
);

PEPROCESS
PsLookupProcessByProcessId(
    IN HANDLE ProcessId
);

PETHREAD
PsLookupThreadByThreadId(
    IN HANDLE ThreadId
);

void
PsFreeCid(
    IN HANDLE Cid
);

void*
PspFindMtdllEntryRva(
    IN PFILE_OBJECT MtdllObject,
    IN const char* RoutineName
);

uintptr_t
PspFindMtdllEntryAddress(
    IN const char* RoutineName,
    IN PETHREAD Thread
);

void
PspStartThread(
    IN PETHREAD Thread
);

void
MepEnqueueReadyThreadLocked(
    IN PETHREAD Thread,
    IN PREADY_QUEUE ReadyQueue
);

// Enqueues a thread into the queue with spinlock protection.
FORCEINLINE
void
MeEnqueueThreadWithLock(
    PREADY_QUEUE Queue,
    PETHREAD Thread
)
{
    IRQL OldIrql;

    MsAcquireSpinlock(&Queue->Lock, &OldIrql);
    MsAcquireSpinlockAtDpcLevel(&Thread->InternalThread.SchedulerLock);

    MepEnqueueReadyThreadLocked(Thread, Queue);

    MsReleaseSpinlockFromDpcLevel(&Thread->InternalThread.SchedulerLock);
    MsReleaseSpinlock(&Queue->Lock, OldIrql);
}

// Dequeues the head thread from the queue with spinlock protection.
FORCEINLINE
PETHREAD
MeDequeueThreadWithLock(
    PREADY_QUEUE Queue
)
{
    IRQL OldIrql;
    MsAcquireSpinlock(&Queue->Lock, &OldIrql);

    // Peek first because we need the thread before removing it.
    if (IsListEmpty(&Queue->ListHead)) {
        MsReleaseSpinlock(&Queue->Lock, OldIrql);
        return NULL;
    }

    PDOUBLY_LINKED_LIST Entry = Queue->ListHead.Flink;
    PETHREAD Thread = CONTAINING_RECORD(
        Entry,
        ETHREAD,
        SchedulerListEntry
    );

    // Lock order remains ready queue, then target scheduler lock.
    MsAcquireSpinlockAtDpcLevel(
        &Thread->InternalThread.SchedulerLock
    );

    RemoveEntryList(Entry);

    InterlockedStoreRelease(
        &Thread->InternalThread.ReadyProcessor,
        NULL
    );

    MsReleaseSpinlockFromDpcLevel(
        &Thread->InternalThread.SchedulerLock
    );
    MsReleaseSpinlock(&Queue->Lock, OldIrql);

    return Thread;
}

// Caller must hold ReadyQueue.Lock followed by Thread->SchedulerLock.
FORCEINLINE
void
MeEnqueueThread(
    PREADY_QUEUE Queue,
    PETHREAD Thread
)
{
    MepEnqueueReadyThreadLocked(Thread, Queue);
}

FORCEINLINE
bool
MeRemoveThreadFromQueue(
    PDOUBLY_LINKED_LIST Queue,
    PETHREAD Thread
)
{
    if (!Queue || !Thread) return false;

    PDOUBLY_LINKED_LIST Target = &Thread->SchedulerListEntry;
    for (PDOUBLY_LINKED_LIST Entry = Queue->Flink;
         Entry != Queue;
         Entry = Entry->Flink) {
        if (Entry != Target) continue;

        RemoveEntryList(Entry);
        return true;
    }

    return false;
}

// Dequeues the head thread from the queue (No Lock).
FORCEINLINE
PETHREAD
MeDequeueThread(
    PDOUBLY_LINKED_LIST Queue
)
{
    PDOUBLY_LINKED_LIST Entry = RemoveHeadList(Queue);
    if (!Entry) return NULL;

    InitializeListHead(Entry);
    return CONTAINING_RECORD(Entry, ETHREAD, SchedulerListEntry);
}
#endif
