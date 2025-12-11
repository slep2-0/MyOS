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
#include "core.h"

// Exception Includes
#include "exception.h"

// ------------------ ENUMERATORS ------------------

typedef enum _THREAD_STATE {
    THREAD_RUNNING,
    THREAD_READY,
    THREAD_BLOCKED,
    THREAD_TERMINATING,
    THREAD_TERMINATED,
    THREAD_ZOMBIE
} THREAD_STATE, *PTHREAD_STATE;

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

//
// Thread Access Rights
//
#define MT_THREAD_TERMINATE          0x0001    // Terminate the thread
#define MT_THREAD_SUSPEND_RESUME     0x0002    // Suspend or resume thread execution
#define MT_THREAD_SET_CONTEXT        0x0004    // Modify thread CPU context (registers, RIP/RSP)
#define MT_THREAD_GET_CONTEXT        0x0008    // Read thread CPU context
#define MT_THREAD_QUERY_INFO         0x0010    // Query thread info (state, priority, etc.)
#define MT_THREAD_SET_INFO           0x0020    // Modify thread info (priority, name, affinity)

#define MT_THREAD_ALL_ACCESS         0x003F    // Request all valid thread access rights


//
// Process Access Rights
//
#define MT_PROCESS_TERMINATE          0x0001  // Kill the process
#define MT_PROCESS_CREATE_THREAD      0x0002  // Create a new thread inside process
#define MT_PROCESS_VM_OPERATION       0x0004  // Allocate/Protect/Free process memory
#define MT_PROCESS_VM_READ            0x0008  // Read from process memory
#define MT_PROCESS_VM_WRITE           0x0010  // Write to process memory
#define MT_PROCESS_DUP_HANDLE         0x0020  // Duplicate a handle into this process
#define MT_PROCESS_SET_INFO           0x0040  // Modify process properties/metadata
#define MT_PROCESS_QUERY_INFO         0x0080  // Query process details (PID, exit code, etc.)
#define MT_PROCESS_SUSPEND_RESUME     0x0100  // Suspend / Resume process

#define MT_PROCESS_ALL_ACCESS         0x01FF  // Everything above

typedef struct _EPROCESS {
    struct _IPROCESS InternalProcess; // Internal process structure. (KPROCESS Equivalent-ish)
    char ImageName[24]; // Process image name - e.g "mtoskrnl.mtexe"
    uint32_t PID; // Process Identifier, unique identifier to the process.
    struct _EPROCESS* ParentProcess; // Parent Process Pointer (should always be one)
    uint32_t priority; // TODO
    uint64_t CreationTime; // Timestamp of creation, seconds from 1970 January 1st. (may change)
    // SID TODO. - User info as well, when users.

    // TODO PEB
    uint64_t ImageBase; // Base Pointer of loaded process memory.

    // Synchorinzation for internal functions.
    struct _RUNDOWN_REF ProcessRundown; // A process rundown that is used to safely synchronize the teardown or deletion of a process, ensuring no threads are still accessing it.

    // Thread infos
    struct _ETHREAD* MainThread; // Pointer to the main thread created for the process.
    struct _Queue AllThreads; // A singular linked list of pointers to the current threads of the process. (inserted with each new creation)
    uint32_t NumThreads; // Unsigned 32 bit integer representing the amount of threads the process has.
    uint64_t NextStackTop; // A 64 bit value representing the next stack top for a newly created thread

    // Handle Table
    PHANDLE_TABLE ObjectTable;

    // VAD
    struct _MMVAD* VadRoot; // The Root of the VAD for the process. (used to find free virtual addresses spaces in the process, and information about them)
    SPINLOCK VadLock; // The spinlock to ensure VAD atomicity.
} EPROCESS, *PEPROCESS;

typedef struct _ETHREAD {
    struct _ITHREAD InternalThread; // Internal thread structure. (KTHREAD Equivalent-ish)
    // TODO TEB
    struct _EXCEPTION_REGISTRATION_RECORD ExceptionRegistration;
    uint32_t TID;           /* thread id */
    struct _EVENT* CurrentEvent; /* ptr to current EVENT if any. */
    struct _EPROCESS* ParentProcess; /* pointer to the parent process of the thread */
    struct _DOUBLY_LINKED_LIST ThreadListEntry; // Forward and backward links to queue threads in.
    struct _RUNDOWN_REF ThreadRundown; // A thread rundown that is used to safely synchronize the teardown or deletion of a thread, ensuring no other threads are still accessing it.
    MTSTATUS ExitStatus; // The status the thread exited in.
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

extern void MsYieldExecution(PTRAP_FRAME threadRegisters);
MTSTATUS PsCreateProcess(const char* path, PEPROCESS* outProcess, PEPROCESS ParentProcess);
MTSTATUS PsCreateThread(PEPROCESS ParentProcess, PETHREAD* outThread, ThreadEntry entry, THREAD_PARAMETER parameter, TimeSliceTicks TIMESLICE);
MTSTATUS PsCreateSystemThread(ThreadEntry entry, THREAD_PARAMETER parameter, TimeSliceTicks TIMESLICE);

MTSTATUS
PsInitializeSystem(
    IN enum _PS_PHASE_ROUTINE Phase
);

void PsDeferKernelStackDeletion(void* StackBase, bool IsLarge);

void
PsTerminateProcess(
    IN PEPROCESS Process
);

void
PsTerminateThread(
    IN PETHREAD Thread,
    IN MTSTATUS ExitStatus
);

void
PsDeleteThread(
    IN void* Object
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

// Will return current running process, or NULL if not present. (shouldn't happen after init)

{
    if (PsGetCurrentThread()) {
        return PsGetCurrentThread()->ParentProcess;
    }
    else return NULL;
}

FORCEINLINE
void
PsTerminateCurrentThread(void) {
    return PsTerminateThread(PsGetCurrentThread(), MT_SUCCESS);
}

FORCEINLINE
void
PsTerminateCurrentProcess(void) {
    return PsTerminateProcess(PsGetCurrentProcess());
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
    if (Thread == NULL) return true;
    return (Thread->ParentProcess == &PsInitialSystemProcess);
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







// End Of Ps API.

// Executive Functions - Are in PS.H
// Enqueues a thread into the queue with spinlock protection.
FORCEINLINE
void
MeEnqueueThreadWithLock(
    Queue* queue, PETHREAD thread)
{
    IRQL flags;
    MsAcquireSpinlock(&queue->lock, &flags);

    // Initialize the new node's links
    thread->ThreadListEntry.Flink = NULL;

    if (queue->tail) {
        // Link new node to current tail
        thread->ThreadListEntry.Blink = &queue->tail->ThreadListEntry;
        // Link current tail to new node
        queue->tail->ThreadListEntry.Flink = &thread->ThreadListEntry;
    }
    else {
        // List was empty
        thread->ThreadListEntry.Blink = NULL;
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

    // Check if there is a next item
    if (t->ThreadListEntry.Flink) {
        // Get the ETHREAD from the generic list entry
        q->head = CONTAINING_RECORD(t->ThreadListEntry.Flink, ETHREAD, ThreadListEntry);
        // The new head has no previous item
        q->head->ThreadListEntry.Blink = NULL;
    }
    else {
        // Queue is now empty
        q->head = NULL;
        q->tail = NULL;
    }

    // Isolate the removed thread
    t->ThreadListEntry.Flink = NULL;
    t->ThreadListEntry.Blink = NULL;

    MsReleaseSpinlock(&q->lock, flags);
    return t;
}

// Enqueues the thread given to the queue (No Lock).
FORCEINLINE
void MeEnqueueThread(Queue* queue, PETHREAD thread)
{
    // Initialize the new node's links
    thread->ThreadListEntry.Flink = NULL;

    if (queue->tail) {
        // Link new node to current tail
        thread->ThreadListEntry.Blink = &queue->tail->ThreadListEntry;
        // Link current tail to new node
        queue->tail->ThreadListEntry.Flink = &thread->ThreadListEntry;
    }
    else {
        // List was empty
        thread->ThreadListEntry.Blink = NULL;
        queue->head = thread;
    }

    // Update tail to be the new thread
    queue->tail = thread;
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
    if (t->ThreadListEntry.Flink) {
        // Get the ETHREAD from the generic list entry
        q->head = CONTAINING_RECORD(t->ThreadListEntry.Flink, ETHREAD, ThreadListEntry);
        // The new head has no previous item
        q->head->ThreadListEntry.Blink = NULL;
    }
    else {
        // Queue is now empty
        q->head = NULL;
        q->tail = NULL;
    }

    // Isolate the removed thread
    t->ThreadListEntry.Flink = NULL;
    t->ThreadListEntry.Blink = NULL;

    return t;
}

#endif