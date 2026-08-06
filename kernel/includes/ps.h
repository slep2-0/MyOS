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
    THREAD_BLOCKING
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
    HANDLE ParentProcess; // Parent Process Handle
    uint32_t priority; // TODO
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
    HANDLE PID;           // Thread's process PID.
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

MTSTATUS
PsCreateProcess(
    IN const char* ExecutablePath,
    OUT PHANDLE ProcessHandle,
    IN ACCESS_MASK DesiredAccess,
    _In_Opt HANDLE ParentProcess
);

MTSTATUS
PsCreateThread(
    PEPROCESS Process,
    PHANDLE ThreadHandle,
    THREAD_START_ROUTINE EntryPoint,
    THREAD_PARAMETER ThreadParameter,
    TimeSliceTicks TimeSlice,
    ThreadEntry MtdllEntrypoint
);

#define MtYield() MsYieldExecution(&PsGetCurrentThread()->InternalThread.TrapRegisters);

extern void MsYieldExecution(PTRAP_FRAME threadRegisters);
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
    // safety guard, can't believe i had to put it.
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

// Enqueues a thread into the queue with spinlock protection.
FORCEINLINE
void
MeEnqueueThreadWithLock(
    Queue* queue, PETHREAD thread)
{
    IRQL flags;
    MsAcquireSpinlock(&queue->lock, &flags);

    // Initialize the new node's links using the SCHEDULER entry
    thread->SchedulerListEntry.Flink = NULL;

    if (queue->tail) {
        // Link new node to current tail
        thread->SchedulerListEntry.Blink = &queue->tail->SchedulerListEntry;
        // Link current tail to new node
        queue->tail->SchedulerListEntry.Flink = &thread->SchedulerListEntry;
    }
    else {
        // List was empty
        thread->SchedulerListEntry.Blink = NULL;
        queue->head = thread;
    }

    // Update tail to be the new thread
    queue->tail = thread;

    MsReleaseSpinlock(&queue->lock, flags);
}

// Dequeues the head thread from the queue with spinlock protection.
FORCEINLINE
PETHREAD
MeDequeueThreadWithLock(Queue* q)
{
    IRQL flags;
    MsAcquireSpinlock(&q->lock, &flags);

    if (!q->head) {
        MsReleaseSpinlock(&q->lock, flags);
        return NULL;
    }

    PETHREAD t = q->head;

    // Check if there is a next item using the SCHEDULER entry
    if (t->SchedulerListEntry.Flink) {
        // Get the ETHREAD from the generic list entry
        // NOTE: We now use SchedulerListEntry for the CONTAINING_RECORD calculation
        q->head = CONTAINING_RECORD(t->SchedulerListEntry.Flink, ETHREAD, SchedulerListEntry);

        // The new head has no previous item
        q->head->SchedulerListEntry.Blink = NULL;
    }
    else {
        // Queue is now empty
        q->head = NULL;
        q->tail = NULL;
    }

    // Isolate the removed thread
    t->SchedulerListEntry.Flink = NULL;
    t->SchedulerListEntry.Blink = NULL;

    MsReleaseSpinlock(&q->lock, flags);
    return t;
}

// Enqueues the thread given to the queue (No Lock).
FORCEINLINE
void MeEnqueueThread(Queue* queue, PETHREAD thread)
{
    // Initialize the new node's links
    thread->SchedulerListEntry.Flink = NULL;

    if (queue->tail) {
        // Link new node to current tail
        thread->SchedulerListEntry.Blink = &queue->tail->SchedulerListEntry;
        // Link current tail to new node
        queue->tail->SchedulerListEntry.Flink = &thread->SchedulerListEntry;
    }
    else {
        // List was empty
        thread->SchedulerListEntry.Blink = NULL;
        queue->head = thread;
    }

    // Update tail to be the new thread
    queue->tail = thread;
}

FORCEINLINE
bool
MeRemoveThreadFromQueue(
    Queue* queue,
    PETHREAD thread
)
{
    if (!queue || !thread) return false;

    bool IsHead = (queue->head == thread);
    bool IsTail = (queue->tail == thread);

    if (!IsHead && !IsTail &&
        thread->SchedulerListEntry.Flink == NULL &&
        thread->SchedulerListEntry.Blink == NULL) {
        return false;
    }

    PETHREAD Next = NULL;
    PETHREAD Prev = NULL;

    if (thread->SchedulerListEntry.Flink) {
        Next = CONTAINING_RECORD(thread->SchedulerListEntry.Flink, ETHREAD, SchedulerListEntry);
    }

    if (thread->SchedulerListEntry.Blink) {
        Prev = CONTAINING_RECORD(thread->SchedulerListEntry.Blink, ETHREAD, SchedulerListEntry);
    }

    if (!Prev && !IsHead) return false;
    if (!Next && !IsTail) return false;

    if (Prev) {
        Prev->SchedulerListEntry.Flink = thread->SchedulerListEntry.Flink;
    }
    else {
        queue->head = Next;
    }

    if (Next) {
        Next->SchedulerListEntry.Blink = thread->SchedulerListEntry.Blink;
    }
    else {
        queue->tail = Prev;
    }

    thread->SchedulerListEntry.Flink = NULL;
    thread->SchedulerListEntry.Blink = NULL;
    return true;
}

// Dequeues the head thread from the queue (No Lock).
FORCEINLINE
PETHREAD MeDequeueThread(Queue* q)
{
    if (!q->head) {
        return NULL;
    }

    PETHREAD t = q->head;

    // Check if there is a next item
    if (t->SchedulerListEntry.Flink) {
        // Get the ETHREAD from the generic list entry
        q->head = CONTAINING_RECORD(t->SchedulerListEntry.Flink, ETHREAD, SchedulerListEntry);

        // The new head has no previous item
        q->head->SchedulerListEntry.Blink = NULL;
    }
    else {
        // Queue is now empty
        q->head = NULL;
        q->tail = NULL;
    }

    // Isolate the removed thread
    t->SchedulerListEntry.Flink = NULL;
    t->SchedulerListEntry.Blink = NULL;

    return t;
}
#endif
