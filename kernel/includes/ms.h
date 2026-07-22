#ifndef X86_MATANEL_SYNCHRONIZATION_H
#define X86_MATANEL_SYNCHRONIZATION_H

/*++

Module Name:

    ms.h

Purpose:

    This module contains the header files & prototypes required for synchronization in a threaded - multiprocessing system.

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "../mtstatus.h"
#include "annotations.h"
#include "core.h"
#include "../intrinsics/atomic.h"

// PLACEHOLDER, until a translation unit includes assert.h
#ifdef __OFFSET_GENERATOR__
#define assert(...) do { } while(0)
#endif

extern void assert_fail(const char* expr, const char* reason, const char* file, const char* func, int line);

// ------------------ STRUCTURES ------------------

/**
 * SPINLOCK - a tiny embedded spinlock representation.
 *
 * Implementation note: keep this embedded (not a pointer) inside structures.
 */
typedef struct _SPINLOCK {
    volatile uint32_t locked; /* 0 = unlocked, 1 = locked */
} SPINLOCK, *PSPINLOCK;

/**
* Rundown Reference Protection.
*
* Used to protect current acquisition of destruction, for example, acquiring a rundown protection on a PROCESS or a Thread to assert they will not be destroyed during modification.
*
*/

typedef struct _RUNDOWN_REF {
    uint64_t Count; // Reference count, bit 0-62 is used for reference counting, bit 63 is used to signify the object is being terminated. (teardown flag)
} RUNDOWN_REF, *PRUNDOWN_REF;
 
// Each CPU has its own lock, they do not use the global scheduler lock.
typedef struct _Queue {
    PETHREAD head;
    PETHREAD tail;
    SPINLOCK lock;
} Queue;

// Every waitable dispatcher object begins with this header. Type selects its
// satisfaction rules, SignalState stores its available signal/permit state,
// Lock protects that state and WaitListHead, and WaitListHead contains the
// embedded wait blocks of threads currently registered on the object.
typedef enum _DISPATCHER_TYPE {
    DispatcherNotificationEvent,
    DispatcherSynchronizationEvent,
    DispatcherSemaphore,
    DispatcherMutex,
    DispatcherThread,
    DispatcherProcess
} DISPATCHER_TYPE;

typedef struct _DISPATCHER_HEADER {
    DISPATCHER_TYPE Type;

    /*
    
    Signal States for each Object:

    ----------------------
    Mutex:

    1 - Mutex Available for anyone to claim
    0 - Mutex held by its owner thread.
    Below 0 - Mutex is recursively acquired by its owner thread.

    Each acquisition decrements SignalState.
    Each release increments SignalState.
    The mutex becomes available again when SignalState reaches 1.


    ----------------------
    Event: (both types)

    1 - Event is signaled
    0 - Event is not signaled


    ----------------------
    Semaphore:

    Above 0 - Available permits; each successful wait consumes one.
    0 - No permits are available, so a nonzero-timeout wait must block.


    ----------------------
    Threads & Processes:

    1 - Thread/Process is terminated and so is signaled.
    0 - Thread/Process is still running.

    Waiting on a terminated thread or process does not consume its signal.


    */

    int32_t SignalState;
    SPINLOCK Lock;
    // Head of WAIT_BLOCK.ObjectListEntry nodes, protected by Lock.
    DOUBLY_LINKED_LIST WaitListHead;
} DISPATCHER_HEADER, * PDISPATCHER_HEADER;

#define MsInitializeDispatcherHeader(Header, InitialSignalState, DispatcherType) \
    do {                                                                         \
        PDISPATCHER_HEADER _Header = (Header);                                   \
        DISPATCHER_TYPE _Type = (DispatcherType);                               \
                                                                                 \
        assert(_Header != NULL);                                                 \
        assert(_Type >= DispatcherNotificationEvent &&                          \
               _Type <= DispatcherProcess);                                     \
                                                                                 \
        _Header->Type = _Type;                                                   \
        _Header->SignalState = (InitialSignalState);                             \
        _Header->Lock.locked = 0;                                                \
        InitializeListHead(&_Header->WaitListHead);                  \
    } while (0)

// Event object. A synchronization event satisfies one waiter per signal;
// a notification event remains signaled and satisfies every waiter.
typedef struct _EVENT {
    DISPATCHER_HEADER Header;
} EVENT, *PEVENT;

/**
* MUTEX - Mutual exclusion.
*
* Used to sleep instead of busy waiting, used in non critical paths (e.g IRQL < DISPATCH_LEVEL)
*
*/
typedef struct _MUTEX {
    DISPATCHER_HEADER Header; // Must remain first so the mutex is waitable as a dispatcher object.
    PETHREAD OwnerThread; // The thread that owns the current mutex, NULL if none.
    // Recursion is encoded by Header.SignalState: 1 is free, 0 is the first
    // acquisition, and negative values represent recursive acquisitions.
    // Set when an owner exits without releasing the mutex. The next owner
    // receives MT_MUTEX_ABANDONED once, warning that protected data may be inconsistent.
    bool Abandoned;
    // Links this mutex into OwnerThread's owned-mutex list. The owning
    // thread's OwnedMutexesListLock protects this entry's list links.
    DOUBLY_LINKED_LIST OwnerListEntry;
} MUTEX, *PMUTEX;

typedef struct _SEMPAHORE {
    DISPATCHER_HEADER Header;
    int32_t Limit;
} SEMAPHORE, *PSEMAPHORE;

typedef struct _PUSH_LOCK {
    union {
        struct {
            uint64_t Locked : 1;
            uint64_t Waiting : 1;
            uint64_t Waking : 1;
            uint64_t MultipleShared : 1;
            uint64_t Shared : 60;
        };
        uint64_t Value;
        void* Pointer;
    };
} PUSH_LOCK;

typedef struct _PUSH_LOCK_WAIT_BLOCK {
    union {
        struct _PUSH_LOCK_WAIT_BLOCK* Next; // Links to the next waiter in the stack
        struct _PUSH_LOCK_WAIT_BLOCK* Last; // Only used if this is the Head node (optimization)
    };

    EVENT WakeEvent;     // The event the thread sleeps on
    uint32_t Flags;      // 1 = Exclusive, 2 = Shared
    uint32_t ShareCount; // If we interrupt readers, we save their count here
    bool Signaled;       // Optimization to avoid touching the Event if not needed
} PUSH_LOCK_WAIT_BLOCK, * PPUSH_LOCK_WAIT_BLOCK;

#define PL_FLAGS_EXCLUSIVE 0x1
#define PL_FLAGS_SHARED    0x2

// Bit definitions for the PUSH_LOCK->Value
#define PL_LOCK_BIT        0x1     // Bit 0: Locked Exclusive
#define PL_WAIT_BIT        0x2     // Bit 1: There are waiters
#define PL_WAKE_BIT        0x4     // Bit 2: Waking (optimization)
#define PL_FLAG_MASK       0xF     // Bottom 4 bits are flags
#define PL_SHARE_INC       0x10    // Shared count starts at Bit 4

// ------------------ FUNCTIONS ------------------

extern SPINLOCK MsTimerQueueLock;
extern DOUBLY_LINKED_LIST MsTimerQueue;

extern POBJECT_TYPE MsEventType;
extern POBJECT_TYPE MsMutexType;

//#ifndef MT_UP
void
MsAcquireSpinlock(
    IN	PSPINLOCK lock,
    IN	PIRQL OldIrql
);

void
MsReleaseSpinlock(
    IN	PSPINLOCK lock,
    IN	IRQL OldIrql
);
/*
#else
#undef MsAcquireSpinlock
#undef MsReleaseSpinlock

#define MsAcquireSpinlock(x, y) (NULL) // NO-OP
#define MsReleaseSpinlock(x, y) (NULL) // NO-OP
#endif
*/

MTSTATUS
MsInitializeMutexObject(
    IN  PMUTEX mut
);

MTSTATUS
MsReleaseMutexObject(
    IN  PMUTEX mut
);

bool
MsAcquireRundownProtection(
    IN	PRUNDOWN_REF rundown
);

void
MsReleaseRundownProtection(
    IN	PRUNDOWN_REF rundown
);

void 
MsWaitForRundownProtectionRelease(
    IN  PRUNDOWN_REF rundown
);

MTSTATUS
MsSetEvent(
    IN PEVENT event
);

MTSTATUS
MsInitializeSynchronization(
    void
);

void
MsAcquireSpinlockAtDpcLevel(
    IN PSPINLOCK Lock
);

void
MsReleaseSpinlockFromDpcLevel(
    IN PSPINLOCK Lock
);

void
MsAcquirePushLockExclusive(
    IN PUSH_LOCK* Lock
);

void
MsReleasePushLockExclusive(
    IN PUSH_LOCK* Lock
);

void
MsAcquirePushLockShared(
    IN PUSH_LOCK* Lock
);

void
MsReleasePushLockShared(
    IN PUSH_LOCK* Lock
);

PITHREAD
GetHeadOfTimerQueue(void);

void
MsInsertTimerQueue(
    IN PITHREAD Thread,
    IN uint64_t WakeupTime
);

bool
MsRemoveTimerQueue(
    IN PITHREAD Thread
);

bool
MsClaimThreadWait(
    IN PITHREAD Thread,
    IN MTSTATUS CompletionStatus
);

void
MsCompleteThreadWait(
    IN PITHREAD Thread
);

void
MsInitializeEvent(
    IN PEVENT Event,
    IN DISPATCHER_TYPE EventDispatcherType, // must be DispatcherSynchronizationEvent or DispatcherNotificationEvent
    IN bool StartSignaled
);

void
MsResetEvent(
    IN PEVENT Event
);

void
MsInitializeSemaphore(
    IN PSEMAPHORE Semaphore,
    IN int32_t Count,
    IN int32_t Limit
);

int32_t
MsReleaseSemaphore(
    IN PSEMAPHORE Semaphore,
    IN int32_t Adjustment
);

MTSTATUS
MsWaitForSingleObject(
    IN void* Object,
    IN PRIVILEGE_MODE WaitMode, // used later for paging out stacks, TODO
    IN bool Alertable,
    IN uint64_t TimeoutMs
);

PITHREAD
MspDequeueNextWaitThreadLocked(
    PDOUBLY_LINKED_LIST HeaderWaitListHead
);

void TimerExpirationDPC(DPC* Dpc, void* Context, void* SysArg1, void* SysArg2);

FORCEINLINE
void
InitializeListHead(
    PDOUBLY_LINKED_LIST Head
)

{
#ifdef DEBUG
    if (Head == NULL)
        assert_fail("Head != NULL", "InitializeListHead: Head is NULL", __FILE__, __func__, __LINE__);
#endif
    Head->Flink = Head;
    Head->Blink = Head;
}

// ->>>> CRASHES IN THESE FUNCTIONS USUALLY BECAUSE INITIALIZELISTHEAD WASNT USED ON THE DOUBLY LINKED LIST !!!!!!!

FORCEINLINE
void
InsertTailList(
    PDOUBLY_LINKED_LIST Head,
    PDOUBLY_LINKED_LIST Entry
)

{
#ifdef DEBUG
    if (Head == NULL)
        assert_fail("Head != NULL", "InsertTailList: Head is NULL", __FILE__, __func__, __LINE__);

    if (Entry == NULL)
        assert_fail("Entry != NULL", "InsertTailList: Entry is NULL", __FILE__, __func__, __LINE__);

    if (Head->Blink == NULL)
        assert_fail("Head->Blink != NULL", "InsertTailList: Head->Blink is NULL! (Uninitialized list?)", __FILE__, __func__, __LINE__);

    if (Head->Blink->Flink != Head)
        assert_fail("Head->Blink->Flink == Head", "InsertTailList: Corrupt list topology!", __FILE__, __func__, __LINE__);
#endif
    PDOUBLY_LINKED_LIST Blink;
    // The last element is the one before Head (circular list style)
    Blink = Head->Blink;
    Entry->Flink = Head;  // New entry points forward to Head
    Entry->Blink = Blink; // New entry points back to old last node
    Blink->Flink = Entry; // Old last node points forward to new entry
    Head->Blink = Entry;  // Head points back to new entry
}

FORCEINLINE
void
InsertHeadList(
    PDOUBLY_LINKED_LIST Head,
    PDOUBLY_LINKED_LIST Entry
)
{
#ifdef DEBUG
    if (Head == NULL)
        assert_fail("Head != NULL", "InsertHeadList: Head is NULL", __FILE__, __func__, __LINE__);

    if (Entry == NULL)
        assert_fail("Entry != NULL", "InsertHeadList: Entry is NULL", __FILE__, __func__, __LINE__);

    if (Head->Flink == NULL)
        assert_fail("Head->Flink != NULL", "InsertHeadList: Head->Flink is NULL! (Uninitialized list?)", __FILE__, __func__, __LINE__);

    if (Head->Flink->Blink != Head)
        assert_fail("Head->Flink->Blink == Head", "InsertHeadList: Corrupt list topology!", __FILE__, __func__, __LINE__);
#endif
    PDOUBLY_LINKED_LIST First;

    // The first element is the one after Head (circular list)
    First = Head->Flink;

    Entry->Flink = First; // Entry -> next = old first
    Entry->Blink = Head;  // Entry -> prev = head

    First->Blink = Entry; // old first -> prev = entry
    Head->Flink = Entry;  // head -> next = entry
}

FORCEINLINE
PDOUBLY_LINKED_LIST
RemoveHeadList(
    PDOUBLY_LINKED_LIST Head
)

{
#ifdef DEBUG
    if (Head == NULL)
        assert_fail("Head != NULL", "RemoveHeadList: Head is NULL!", __FILE__, __func__, __LINE__);

    if (Head->Flink == NULL)
        assert_fail("Head->Flink != NULL", "RemoveHeadList: Head->Flink is NULL! (Uninitialized/Zeroed Memory)", __FILE__, __func__, __LINE__);

    if (Head->Blink == NULL)
        assert_fail("Head->Blink != NULL", "RemoveHeadList: Head->Blink is NULL! (Uninitialized/Zeroed Memory)", __FILE__, __func__, __LINE__);

    if (Head->Flink->Blink != Head)
        assert_fail("Head->Flink->Blink == Head", "RemoveHeadList: Corrupt list topology!", __FILE__, __func__, __LINE__);
#endif
    PDOUBLY_LINKED_LIST Entry;
    PDOUBLY_LINKED_LIST Flink;

    Entry = Head->Flink;
    if (Entry == Head) {
        // List is empty
        return NULL;
    }

#ifdef DEBUG
    if (Entry->Flink == NULL)
        assert_fail("Entry->Flink != NULL", "RemoveHeadList: Entry->Flink is NULL!", __FILE__, __func__, __LINE__);
#endif

    Flink = Entry->Flink;
    Head->Flink = Flink;
    Flink->Blink = Head;

    // Clear links
    Entry->Flink = Entry->Blink = NULL;
    return Entry;
}

FORCEINLINE
void
RemoveEntryList(
    PDOUBLY_LINKED_LIST Entry
)
{
#ifdef DEBUG
    if (Entry == NULL)
        assert_fail("Entry != NULL", "RemoveEntryList: Entry is NULL", __FILE__, __func__, __LINE__);

    if (Entry->Flink == NULL || Entry->Blink == NULL)
        assert_fail("Entry->Flink != NULL && Entry->Blink != NULL", "RemoveEntryList: Entry links are NULL! (Double remove or uninitialized?)", __FILE__, __func__, __LINE__);

    if (Entry->Flink->Blink != Entry)
        assert_fail("Entry->Flink->Blink == Entry", "RemoveEntryList: Corrupt forward link!", __FILE__, __func__, __LINE__);

    if (Entry->Blink->Flink != Entry)
        assert_fail("Entry->Blink->Flink == Entry", "RemoveEntryList: Corrupt backward link!", __FILE__, __func__, __LINE__);
#endif
    PDOUBLY_LINKED_LIST Flink;
    PDOUBLY_LINKED_LIST Blink;

    Flink = Entry->Flink;
    Blink = Entry->Blink;

    /* Normal (minimal) unlink — identical to Windows' RemoveEntryList */
    Blink->Flink = Flink;
    Flink->Blink = Blink;

    // Sanitize the removed entry so it doesn't look valid
    Entry->Flink = Entry;
    Entry->Blink = Entry;
}

// Warning, this will overwrite the popped entry from the "Head" parameter links, so they will point to themselves.
FORCEINLINE 
PDOUBLY_LINKED_LIST
PopHeadAndRestoreLinks(
    PDOUBLY_LINKED_LIST Head
)

{
    PDOUBLY_LINKED_LIST ToRestore = RemoveHeadList(Head);
    InitializeListHead(ToRestore);
    return ToRestore;
}

FORCEINLINE
bool
IsListEmpty(
    IN PDOUBLY_LINKED_LIST Head
)

{
    return (Head->Flink == Head);
}


/* Interlocked push: atomically push Entry onto *ListHeadPtr.
   ListHeadPtr is PSINGLE_LINKED_LIST* (address of the head pointer). 
   Usage: InterlockedPushEntry(&Descriptor->FreeListHead.Next, &Header->Metadata.FreeListEntry);
   */
FORCEINLINE
void
InterlockedPushEntry(
    PSINGLE_LINKED_LIST* ListHeadPtr, /* &head_ptr */
    PSINGLE_LINKED_LIST Entry         /* entry->Next must be valid memory */
)
{
    PSINGLE_LINKED_LIST oldHead;
    do {
        oldHead = InterlockedLoadRelaxed(ListHeadPtr);
        Entry->Next = oldHead;
        /* CompareExchange returns the head value it actually observed. */
    } while (InterlockedCompareExchangePointer(
        (volatile void* volatile*)ListHeadPtr,
        Entry,
        oldHead
    ) != oldHead);
}

/* Interlocked pop: atomically pop and return the old head (or NULL).
   Returns the popped entry pointer. 
   Usage: InterlockedPopEntry(&Descriptor->FreeListHead.Next);
   */
FORCEINLINE
PSINGLE_LINKED_LIST
InterlockedPopEntry(
    PSINGLE_LINKED_LIST* ListHeadPtr
)
{
    PSINGLE_LINKED_LIST oldHead;
    PSINGLE_LINKED_LIST next;

    do {
        oldHead = InterlockedLoadAcquire(ListHeadPtr);
        if (oldHead == NULL)
            return NULL;
        next = oldHead->Next;
        /* A mismatched observed head means another CPU won; retry. */
    } while (InterlockedCompareExchangePointer(
        (volatile void* volatile*)ListHeadPtr,
        next,
        oldHead
    ) != oldHead);
    return oldHead;
}

#endif // X86_MATANEL_SYNCHRONIZATION_H
