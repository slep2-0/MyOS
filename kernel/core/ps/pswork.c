/*++

Module Name:

    pswork.c

Purpose:

    This translation unit contains the implementation of worker threads in the kernel.

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "../../includes/ps.h"
#include "../../includes/mg.h"
#include "../../assert.h"
// globals
volatile void* g_StackReaperList = NULL; // head of LIFO list (casts to PSTACK_REAPER_ENTRY)
EVENT g_StackReaperEvent;

// atomically pop all entries (returns head or NULL)
FORCEINLINE PSTACK_REAPER_ENTRY PopAllStacks(void)
{
    return (PSTACK_REAPER_ENTRY)InterlockedExchangePointer((volatile void**)&g_StackReaperList, NULL);
}


static void PsStackDeleterThread(void) {
#ifdef DEBUG
    gop_printf(COLOR_RED, "I have arrived, the reaper of souls n shit (and stacks)\n");
#endif
    for (;;) {
        // Wait until there is work (or force wake).
        MsWaitForEvent(&g_StackReaperEvent);

        // Atomically steal the whole list
        PSTACK_REAPER_ENTRY head = PopAllStacks();

        // If nothing (possible if race condition), continue waiting again
        if (!head) {
            continue;
        }

        // Walk and free each stack entry (safe at PASSIVE_LEVEL)
        while (head) {
            PSTACK_REAPER_ENTRY cur = head;
            head = cur->Next;

            // free the kernel stack safely from this thread's stack
            MiFreeKernelStack(cur->StackBase, cur->IsLarge);

            // free the node
            MmFreePool(cur);
        }

        // Loop back to wait for more work, if there is work, i work, on fridays, i work, saturdays - work too.
    }
}

void PsDeferKernelStackDeletion(void* StackBase, bool IsLarge)
{
    PSTACK_REAPER_ENTRY node = MmAllocatePoolWithTag(NonPagedPool, sizeof(STACK_REAPER_ENTRY), 'rSpR');
    if (!node) return;

    node->StackBase = StackBase;
    node->IsLarge = IsLarge;

    void* old;
    do {
        old = (void*)g_StackReaperList;
        node->Next = (PSTACK_REAPER_ENTRY)old;
        // cast target to volatile void** to match prototype
    } while (InterlockedCompareExchangePointer((volatile void* volatile*)&g_StackReaperList, (void*)node, old) != old);

    // Wake the reaper (safe from any context)
#ifdef DEBUG
    MTSTATUS status = MsSetEvent(&g_StackReaperEvent);
    assert(MT_SUCCEEDED(status));
#else
    MsSetEvent(&g_StackReaperEvent);
#endif
}

void PsInitializeWorkerThreads(void) {
    // Setup the event.
    g_StackReaperEvent.lock.locked = 0;
    g_StackReaperEvent.signaled = false;
    g_StackReaperEvent.type = SynchronizationEvent;
    g_StackReaperEvent.waitingQueue.head = g_StackReaperEvent.waitingQueue.tail = NULL;

    // We just create a system thread for freeing stacks.
    MTSTATUS status = PsCreateSystemThread((ThreadEntry)PsStackDeleterThread, NULL, LOW_TIMESLICE_TICKS);

    if (MT_FAILURE(status)) {
        MeBugCheckEx(
            PSWORKER_INIT_FAILED,
            (void*)(uintptr_t)status,
            NULL,
            NULL,
            NULL
        );
    }
}

