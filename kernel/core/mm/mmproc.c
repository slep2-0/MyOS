/*++

Module Name:

    mmproc.c

Purpose:

    This translation unit contains the implementation of process supporting memory management routines.

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "../../includes/mm.h"
#include "../../includes/me.h"
#include "../../assert.h"
#include "../../includes/mg.h"

void*
MiCreateKernelStack(
    IN  bool LargeStack
)

/*++

    Routine description:

        Creates a kernel stack for use in threads.

    Arguments:

        [IN]    bool LargeStack - Determines if the stack allocated should be MI_LARGE_STACK_SIZE bytes long. (default is MI_STACK_SIZE)

    Return Values:

        Pointer to guard page, or NULL on failure.

        Since the pointer is returned at the very end of the guard page (or start, the stack grows downwards).
        You MUST subtract space from the stack BEFORE interacting with it, as emitting the PUSH instruction before
        subtracting space from the stack WILL cause a page fault.

--*/

{
    // Declarations
    size_t StackSize = LargeStack ? MI_LARGE_STACK_SIZE : MI_STACK_SIZE;
    size_t GuardSize = VirtualPageSize;
    size_t TotalSize = StackSize + GuardSize;
    size_t PagesToMap = BYTES_TO_PAGES(StackSize);

    // Allocate VA range, the stack + guard page.
    uintptr_t BaseVa = MiAllocatePoolVa(NonPagedPool, TotalSize);
    if (!BaseVa) return NULL;

    // Define where we actually start mapping, we skip the Guard page as we obviously dont want to map it.
    uintptr_t MapStartVa = BaseVa + GuardSize;

    size_t Iterations = 0;
    bool failure = false;

    for (size_t i = 0; i < PagesToMap; i++) {
        // Calculate current VA to map
        uintptr_t currVa = MapStartVa + (i * VirtualPageSize);

        PAGE_INDEX pfn = MiRequestPhysicalPage(PfnStateZeroed);

        if (pfn == PFN_ERROR) {
            failure = true;
            break;
        }

        PMMPTE pte = MiGetPtePointer(currVa);
        if (!pte) {
            MiReleasePhysicalPage(pfn);
            failure = true;
            break;
        }

        // Map the Stack Page
        MI_WRITE_PTE(pte, currVa, PFN_TO_PHYS(pfn), PAGE_PRESENT | PAGE_RW);
        Iterations++;
    }

    PMMPTE GuardPte = MiGetPtePointer(BaseVa);

    if (!GuardPte) {
        // Now, I could continue and just not mark the GuardPte as a guard page, as it is only used in bugcheck
        // debugging, but I want to make my debugging life easier.
        // I don't even have a stable kernel debugger, so excuse me for the horrifying line im about to put below.
        MeBugCheckEx(MANUALLY_INITIATED_CRASH,
            (void*)RETADDR(0),
            (void*)BaseVa,
            (void*)TotalSize,
            (void*)123432 /* special identifier for manually initiated crash to know its here */
        );
        // If the bugcheck is ever removed, it would be afailure.
        failure = true;

    }

    if (failure) goto failure_cleanup;

    // Set the Guard page bit in the GuardPte.
    GuardPte->Hard.Present = 0;
    GuardPte->Soft.SoftwareFlags |= MI_GUARD_PAGE_PROTECTION;

    // Return the TOP of the stack.
    return (void*)(BaseVa + TotalSize);

failure_cleanup:
    // Unmap the pages we successfully mapped
    for (size_t j = 0; j < Iterations; j++) {
        uintptr_t vaToFree = MapStartVa + (j * VirtualPageSize);
        PMMPTE pte = MiGetPtePointer(vaToFree);

        if (pte && pte->Hard.Present) {
            PAGE_INDEX pfn = MiTranslatePteToPfn(pte);
            MiUnmapPte(pte);
            MiReleasePhysicalPage(pfn);
        }
    }

    // Free the VA reservation
    if (BaseVa) {
        MiFreePoolVaContiguous(BaseVa, TotalSize, NonPagedPool);
    }

    return NULL;
}

void
MiFreeKernelStack(
    IN void* AllocatedStackTop,
    IN bool LargeStack
)

/*++

    Routine description:

        Frees the stack given to a kernel thread.

    Arguments:

        [IN]    void* AllocatedStackBase - The pointer given by MiCreateKernelStack
        [IN]    bool LargeStack - Signifies if the stack being deleted is a MI_LARGE_STACK_SIZE bytes long (true), or MI_STACK_SIZE bytes long (false)

    Return Values:

        None.

--*/

{
    //gop_printf(COLOR_PINK, "**Reached MiFreeKernelStack | LargeStack: %s | AllocatedStackTop: %p**", (LargeStack ? "True" : "False"), AllocatedStackTop);
    // Declarations
    size_t StackSize = LargeStack ? MI_LARGE_STACK_SIZE : MI_STACK_SIZE;
    size_t GuardSize = VirtualPageSize;
    size_t TotalSize = StackSize + GuardSize;
    size_t PagesToUnMap = BYTES_TO_PAGES(StackSize);

    // 1. Calculate the START of the stack memory (The highest valid byte addressable page)
    // AllocatedStackTop is the byte *after* the stack end. 
    // We start at Top - PageSize.
    uintptr_t CurrentVA = (uintptr_t)AllocatedStackTop - VirtualPageSize;

    for (size_t i = 0; i < PagesToUnMap; i++) {
        PMMPTE pte = MiGetPtePointer(CurrentVA);

        if (pte && pte->Hard.Present) {

            // Get its PFN that was allocated to it.
            PAGE_INDEX pfn = MiTranslatePteToPfn(pte);

            // Unmap the PTE.
            MiUnmapPte(pte);

            // Release the physical page back to the PFN DB.
            MiReleasePhysicalPage(pfn);
        }

        // Move down to the next page
        CurrentVA -= VirtualPageSize;
    }

    // The Guard Page is at the very bottom of the allocation.
    uintptr_t BaseVa = (uintptr_t)AllocatedStackTop - TotalSize;

    PMMPTE GuardPte = MiGetPtePointer(BaseVa);
    if (GuardPte) {
        assert((GuardPte->Soft.SoftwareFlags & MI_GUARD_PAGE_PROTECTION) != 0, "The guard page must have the GUARD_PAGE_PROTECTION bit set.");
        // Remove the software guard flag
        GuardPte->Soft.SoftwareFlags &= ~MI_GUARD_PAGE_PROTECTION;

        // Ensure we aren't accidentally unmapping a present page (Guard pages should be non-present)
        assert(GuardPte->Hard.Present == false);
    }

    // Free the Virtual Address allocation
    MiFreePoolVaContiguous(BaseVa, TotalSize, NonPagedPool);
}