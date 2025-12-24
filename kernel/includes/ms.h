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
 
typedef struct _Queue {
    PETHREAD head;
    PETHREAD tail;
    struct _SPINLOCK lock; /* embedded spinlock (do not change from embedded) */
} Queue;

/**
 * EVENT_TYPE - controls wake behavior
 */
typedef enum _EVENT_TYPE {
    NotificationEvent,   /* wake all waiting threads */
    SynchronizationEvent /* wake one thread at a time */
} EVENT_TYPE;

/**
 * EVENT - kernel event object
 * - Embedded SPINLOCK and Queue for waiting threads.
 */
typedef struct _EVENT {
    enum _EVENT_TYPE type;              /* Notification vs Synchronization */
    volatile bool signaled;             /* current state */
    struct _SPINLOCK lock;                /* protects signaled + waitingQueue */
    struct _Queue waitingQueue;           /* threads waiting on this event */
} EVENT, *PEVENT;

/**
* MUTEX - Mutual exclusion.
*
* Used to sleep instead of busy waiting, used in non critical paths (e.g IRQL < DISPATCH_LEVEL)
*
*/
typedef struct _MUTEX {
    uint32_t ownerTid;  /* owning thread id (0 if none) */
    struct _EVENT SynchEvent;   /* event used for waking waiters */
    bool locked;        /* fast-check boolean (protected by lock) */
    struct _SPINLOCK lock;      /* protects ownerTid/locked and wait list */
    struct _ETHREAD* ownerThread; /* pointer to current thread that holds the mutex */
} MUTEX, *PMUTEX;

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
MsAcquireMutexObject(
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
MsWaitForEvent(
    IN  PEVENT event
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

FORCEINLINE
void
InitializeListHead(
    PDOUBLY_LINKED_LIST Head
)

{
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
    PDOUBLY_LINKED_LIST Entry;
    PDOUBLY_LINKED_LIST Flink;

    Entry = Head->Flink;
    if (Entry == Head) {
        // List is empty
        return NULL;
    }

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
        oldHead = __atomic_load_n(ListHeadPtr, __ATOMIC_RELAXED);
        Entry->Next = oldHead;
        /* try to replace head with Entry */
    } while (!__atomic_compare_exchange_n(
        ListHeadPtr,           /* target */
        &oldHead,              /* expected (updated on failure) */
        Entry,                 /* desired */
        /*weak*/ false,
        __ATOMIC_RELEASE,      /* success: release so prior stores are visible */
        __ATOMIC_RELAXED));    /* failure: relaxed */
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
        oldHead = __atomic_load_n(ListHeadPtr, __ATOMIC_ACQUIRE);
        if (oldHead == NULL)
            return NULL;
        next = oldHead->Next;
        /* try to set head to next */
    } while (!__atomic_compare_exchange_n(
        ListHeadPtr,
        &oldHead,
        next,
        /*weak*/ false,
        __ATOMIC_ACQ_REL,      /* success: acquire+release to pair with push */
        __ATOMIC_RELAXED));   /* failure ordering */
    return oldHead;
}

#endif // X86_MATANEL_SYNCHRONIZATION_H