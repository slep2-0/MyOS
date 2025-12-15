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

        Creates a kernel stack for general use.
        The stack cannot be accessible in user mode (assert(cpl == 0);)

    Arguments:

        [IN]    bool LargeStack - Determines if the stack allocated should be MI_LARGE_STACK_SIZE bytes long. (default is MI_STACK_SIZE)

    Return Values:

        Pointer to top of the stack, or NULL on failure.

    Notes:

        The previous comment stated that it would return at the end of the guard page, which was incorrect when I went through my code.
        This means you CAN emit the PUSH instruction, as it will also subtract space from the stack automatically (based on the pushed immediate).
        But do not subtract too much (hit the guard page), or add to this pointer (hit the next page, could very well be unmapped, or a guard page of another thread)
        - As you risk a page fault.

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
        // If the bugcheck is ever removed, it would be a failure.
        failure = true;

    }

    if (failure) goto failure_cleanup;

    // Clean the PTE.
    GuardPte->Value = 0;

    // Set the Guard page bit in the GuardPte.
    GuardPte->Hard.Present = 0;
    GuardPte->Soft.SoftwareFlags |= MI_GUARD_PAGE_PROTECTION;

    // Invalidate the guard page VA.
    MiInvalidateTlbForVa((void*)BaseVa);

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

    assert(false, "This function is currently a Must-Succeed.");
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
    gop_printf(COLOR_PINK, "**Reached MiFreeKernelStack | LargeStack: %s | AllocatedStackTop: %p**\n", (LargeStack ? "True" : "False"), AllocatedStackTop);
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
        // Clean the page.
        GuardPte->Value = 0;
    }

    // Invalidate the VA for the Guard Page.
    MiInvalidateTlbForVa((void*)BaseVa);

    // Free the Virtual Address allocation
    MiFreePoolVaContiguous(BaseVa, TotalSize, NonPagedPool);
}

MTSTATUS
MmCreateProcessAddressSpace(
    OUT void** DirectoryTable
)

/*++

    Routine description:

        Creates a new paging address space for the process.

    Arguments:

        [OUT] void** DirectoryTable - Pointer to set the newly physical address of the process's CR3.

    Return Values:

        None.

--*/

{
    // Declarations
    PAGE_INDEX pfnIndex;
    uint64_t* pml4Base;
    IRQL oldIrql;
    uint64_t physicalAddress;

    // Allocate a physical page for the PML4.
    pfnIndex = MiRequestPhysicalPage(PfnStateZeroed);

    if (pfnIndex == PFN_ERROR) {
        return MT_NO_RESOURCES;
    }

    // Convert the Index to a Physical Address (needed for CR3 and Recursive entry)
    physicalAddress = PPFN_TO_PHYSICAL_ADDRESS(INDEX_TO_PPFN(pfnIndex));

    // Map the physical page into hypermap so we can edit it temporarily.
    pml4Base = (uint64_t*)MiMapPageInHyperspace(pfnIndex, &oldIrql);

    if (!pml4Base) {
        // If hyperspace mapping fails, release the page and fail.
        MiReleasePhysicalPage(pfnIndex);
        return MT_GENERAL_FAILURE;
    }

    // Setup our recursive mapping.
    MMPTE recursivePte;
    recursivePte.Value = 0;
    recursivePte.Hard.Present = 1;
    recursivePte.Hard.Write = 1;
    recursivePte.Hard.User = 0; // Accessible only by Kernel
    recursivePte.Hard.NoExecute = 1; // Data only
    recursivePte.Hard.PageFrameNumber = (uint64_t)pfnIndex & ((1ULL << 40) - 1); // PFN of itself

    // Write to index 0x1FF (511)
    pml4Base[RECURSIVE_INDEX] = recursivePte.Value;

    // Copy Kernel Address Space.
    // The higher half of memory (Kernel Space) is shared across all processes.
    uint64_t* currentPml4 = pml4_from_recursive();

    // This copies the PML4 from PhysicalMemoryOffset all the way ot the end of the 48bit addressing.
    // Excluding user regions.
    for (int i = MiConvertVaToPml4Offset(PhysicalMemoryOffset); i < 512; i++) {
        pml4Base[i] = currentPml4[i];
    }

    // Ensure it is stored.
    MmBarrier();

    // Unmap from Hyperspace.
    MiUnmapHyperSpaceMap(oldIrql);

    // Return the Physical Address.
    // The scheduler will load this into CR3 when switching to this process.
    *DirectoryTable = (void*)physicalAddress;

    return MT_SUCCESS;
}