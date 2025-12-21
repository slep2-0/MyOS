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
#include "../../includes/ps.h"

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

    // Copy Kernel Address Space.
    // The higher half of memory (Kernel Space) is shared across all processes.
    uint64_t* currentPml4 = pml4_from_recursive();

    // This copies the PML4 from PhysicalMemoryOffset all the way ot the end of the 48bit addressing.
    // Excluding user regions.
    for (int i = MiConvertVaToPml4Offset(PhysicalMemoryOffset); i < 512; i++) {
        pml4Base[i] = currentPml4[i];
    }

    MMPTE recursivePte;
    kmemset(&recursivePte, 0, sizeof(MMPTE)); // Ensure clean start

    // Note: We pass NULL for the VA as it's a self-ref, we only care about the PFN and Flags.
    // Ensure PFN_TO_PHYS is used if MI_WRITE_PTE expects a physical address.
    MI_WRITE_PTE(&recursivePte,
        (void*)0,
        PFN_TO_PHYS(pfnIndex),
        PAGE_PRESENT | PAGE_RW);

    // Write to index 0x1FF (511)
    pml4Base[RECURSIVE_INDEX] = recursivePte.Value;

    // Ensure it is stored.
    MmFullBarrier();

    // Unmap from Hyperspace.
    MiUnmapHyperSpaceMap(oldIrql);

    // Return the Physical Address.
    // The scheduler will load this into CR3 when switching to this process.
    *DirectoryTable = (void*)physicalAddress;

    return MT_SUCCESS;
}

static
void
MiFreePageTableHierarchy(
    IN PAGE_INDEX TablePfn,
    IN int Level
)

/*++

    Routine description:

        Recursively deletes the page tables in hierarchial order (pml4,pdpt,pde,pt)

    Arguments:

        [IN] PAGE_INDEX TablePfn - The PML4 PFN to start from.
        [IN] int Level - The level to start from (4 = PML4)

    Return Values:

        None.

--*/

{
    uint64_t* mapping;
    IRQL oldIrql;
    int limit = 512;
    int start = 0;

    // If this is the PML4:
    // We set the limit of removal to the PhysicalMemoryOffset (like the limit in MmCreateProcessAddressSpace)
    // So we dont remove kernel page tables, as we would cause a triple fault.
    if (Level == 4) {
        limit = MiConvertVaToPml4Offset(PhysicalMemoryOffset);
    }

    // Iterate through the indices.
    for (int i = start; i < limit; i++) {

        PAGE_INDEX childPfn = PFN_ERROR;
        bool isPresent = false;
        bool isLargePage = false;

        // Map the table to read the entry at i
        mapping = (uint64_t*)MiMapPageInHyperspace(TablePfn, &oldIrql);
        MMPTE pte;
        pte.Value = mapping[i];

        if (pte.Hard.Present) {
            isPresent = true;
            childPfn = MiTranslatePteToPfn(&pte);

            // We dont support large pages yet (or we do and I didnt update this comment)
            // But we will scan for them anyway to prevent bugs in the future (faults and such)
            if (Level > 1 && (pte.Value & PAGE_PS)) {
                isLargePage = true;
            }
        }

        // Unmap immediately so we can use Hyperspace in the recursion
        MiUnmapHyperSpaceMap(oldIrql);

        // Process the entry if it was valid
        if (isPresent && childPfn != PFN_ERROR) {

            if (Level > 1) {
                if (isLargePage) {
                    // It's a 2MB or 1GB user page. Release the physical memory directly.
                    MiReleasePhysicalPage(childPfn);
                }
                else {
                    // It's a pointer to a lower-level page table. Recurse.
                    MiFreePageTableHierarchy(childPfn, Level - 1);
                }
            }
            else {
                // The PTs, the vad should have already freed them, but if it didnt, we do it.
                MiReleasePhysicalPage(childPfn);
            }
        }
    }

    // All children are freed, we can free the actual table now.
    MiReleasePhysicalPage(TablePfn);
}

MTSTATUS
MmDeleteProcessAddressSpace(
    IN PEPROCESS Process,
    IN uintptr_t PageDirectoryPhysical
)

/*++

    Routine description:

        Deletes a process address space.

    Arguments:

        [IN] PEPROCESS Process - The process to delete the address space from.
        [IN] uintptr_T PageDirectoryPhysical - Physical address of the process's address space. (CR3)

    Return Values:

        MTSTATUS Status code.

--*/

{
    // Parameter check.
    if (!Process || !PageDirectoryPhysical) {
        return MT_INVALID_PARAM;
    }

    // Convert the physical address to its index.
    PAGE_INDEX pml4Pfn = PHYS_TO_INDEX(PageDirectoryPhysical);

    if (pml4Pfn == PFN_ERROR || !MiIsValidPfn(pml4Pfn)) {
        return MT_INVALID_PARAM;
    }

    // Recursively tear down the page table.
    MiFreePageTableHierarchy(pml4Pfn, 4);

    // Flush CR3 across all processors.
    MiReloadTLBs();

    return MT_SUCCESS;
}

MTSTATUS
MmCreateUserStack(
    IN PEPROCESS Process,
    OUT void** OutStackTop,
    _In_Opt size_t StackReserveSize
)

/*++

    Routine description:

        Creates a stack for a user thread in the process address space (with a guard page below)

    Arguments:

        [IN] PEPROCESS Process - The thread's process.
        [OUT] void** OutStackTop - Top of stack allocated if successful.
        [IN OPTIONAL] size_t StackReserveSize - A value that indicates how much data to reserve for the stack. If not supplied, MI_DEFAULT_USER_STACK_SIZE is used.

    Return Values:

        MTSTATUS Status code.

    Notes:

        If a process allocated too much virtual memory, his next allocation could at Process->NextStackHint
        Which means, the Status will return MT_CONFLICTING_ADDRESSES, which mean thread creation failure.
--*/

{
    // If no stack reserve size, we use the default
    if (!StackReserveSize) StackReserveSize = MI_DEFAULT_USER_STACK_SIZE;

    // Acquire the exclusive push lock for the stack.
    MsAcquirePushLockExclusive(&Process->AddressSpaceLock);

    // Grab current hint.
    uintptr_t CurrentStackHint = Process->NextStackHint;
    
    // Compute the end of the stack.
    uintptr_t EndOfStack = CurrentStackHint - StackReserveSize;

    // Allocate a VAD for the address space.
    MTSTATUS Status = MmAllocateVirtualMemory(Process, (void**) & EndOfStack, StackReserveSize, VAD_FLAG_WRITE | VAD_FLAG_READ);
    if (MT_FAILURE(Status)) goto Cleanup;

    // Create a VAD for the guard page (reserved)

    void* GuardPageEnd = (void*)(EndOfStack - VirtualPageSize);
    Status = MmAllocateVirtualMemory(Process, (void**)&GuardPageEnd, VirtualPageSize, VAD_FLAG_RESERVED);
    if (MT_FAILURE(Status)) goto CleanupWithVad;

    // The next hint should be the end of the guard page.
    Process->NextStackHint = (uintptr_t)GuardPageEnd;
    // Success.
    if (OutStackTop) *OutStackTop = (void*)CurrentStackHint;
    goto Cleanup;

CleanupWithVad:
    MmFreeVirtualMemory(Process, (void*)EndOfStack);

Cleanup:
    MsReleasePushLockExclusive(&Process->AddressSpaceLock);
    return Status;
}