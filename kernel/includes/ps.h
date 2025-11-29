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

// ------------------ STRUCTURES ------------------

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

    // VAD
    struct _MMVAD* VadRoot; // The Root of the VAD for the process. (used to find free virtual addresses spaces in the process, and information about them)
    SPINLOCK VadLock; // The spinlock to ensure VAD atomicity.
} EPROCESS, *PEPROCESS;

typedef struct _ETHREAD {
    struct _ITHREAD InternalThread; // Internal thread structure. (KTHREAD Equivalent-ish)
    struct _ETHREAD* nextThread;     /* singly-linked list pointer for queues */
    // TODO TEB
    struct _EXCEPTION_REGISTRATION_RECORD ExceptionRegistration;
    uint32_t TID;           /* thread id */
    struct _EVENT* CurrentEvent; /* ptr to current EVENT if any. */
    struct _EPROCESS* ParentProcess; /* pointer to the parent process of the thread */
    struct _RUNDOWN_REF ThreadRundown; // A thread rundown that is used to safely synchronize the teardown or deletion of a thread, ensuring no other threads are still accessing it.
    /* TODO: priority, affinity, wait list, etc. */
} ETHREAD, *PETHREAD;

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

PETHREAD
PsGetCurrentThread(
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

// Executive Functions - Are in PS.H
FORCEINLINE
void
MeEnqueueThreadWithLock(
    Queue* queue, PETHREAD thread) {
    IRQL flags;
    MsAcquireSpinlock(&queue->lock, &flags);
    thread->nextThread = NULL;
    if (!queue->head) queue->head = thread;
    else queue->tail->nextThread = thread;
    queue->tail = thread;
    MsReleaseSpinlock(&queue->lock, flags);
}

// Dequeues the current thread from the queue, returns null if none. (acquires spinlock)
FORCEINLINE
PETHREAD
MeDequeueThreadWithLock(Queue* q) {
    IRQL flags;
    MsAcquireSpinlock(&q->lock, &flags);
    if (!q->head) {
        MsReleaseSpinlock(&q->lock, flags); // bye bye comment
        return NULL;
    }

    PETHREAD t = q->head;
    q->head = t->nextThread;
    if (!q->head) {
        q->tail = NULL;
    }
    t->nextThread = NULL;
    MsReleaseSpinlock(&q->lock, flags);
    return t;
}

// Enqueues the thread given to the queue.
FORCEINLINE
void MeEnqueueThread(Queue* queue, PETHREAD thread) {
    thread->nextThread = NULL;
    if (!queue->head) queue->head = thread;
    else queue->tail->nextThread = thread;
    queue->tail = thread;
}

// Dequeues the current thread from the queue, returns null if none.
FORCEINLINE
PETHREAD MeDequeueThread(Queue* q) {
    if (!q->head) {
        return NULL;
    }

    PETHREAD t = q->head;
    q->head = t->nextThread;
    if (!q->head) {
        q->tail = NULL;
    }
    t->nextThread = NULL;
    return t;
}

#endif