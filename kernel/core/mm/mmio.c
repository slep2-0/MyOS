/*++

Module Name:

    mmio.c

Purpose:

    This translation unit contains the implementation of MMIO functions responsible for easy interaction with physical hardware.

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "../../includes/mm.h"
#include "../../includes/me.h"
#include "../../assert.h"


bool
MiCheckForContigiousMemory(
    IN void* StartAddress,
    IN size_t NumberOfBytes
)

/*++

    Routine description:

        Checks if the given address + amount of bytes is contigious in physical memory.

    Arguments:

        [IN]    void* StartAddress - The base address to check for.
        [IN]    size_t NumberOfBytes - The amount of contigious bytes to check.

    Return Values:

        True if contigious, false otherwise.

--*/

{
    // Assertions & Declarations
    assert(NumberOfBytes > 0);
    assert(StartAddress != 0);
    if (!NumberOfBytes || !StartAddress) return false;

    size_t AmtPages = BYTES_TO_PAGES(NumberOfBytes);
    uintptr_t CurrentAddress = (uintptr_t)StartAddress;

    // Get the First PFN.
    PMMPTE CurrentPte = MiGetPtePointer(CurrentAddress);

    // Check if PTE exists and is valid before translating
    if (!CurrentPte || !CurrentPte->Hard.Present) return false;

    PAGE_INDEX StartPfn = MiTranslatePteToPfn(CurrentPte);
    if (StartPfn == PFN_ERROR) return false;

    // 3. Loop from i = 1 (we already checked the first page)
    for (size_t i = 1; i < AmtPages; i++) {

        // Advance VA.
        CurrentAddress += VirtualPageSize;
        CurrentPte = MiGetPtePointer(CurrentAddress);

        // Check if page is even present.
        if (!CurrentPte || !CurrentPte->Hard.Present) return false;

        PAGE_INDEX CurrentPfn = MiTranslatePteToPfn(CurrentPte);

        // If the current Pfn isn't adjacent to the previous one, its not contigious.
        if (CurrentPfn != (StartPfn + i)) {
            return false;
        }
    }

    return true;
}

void*
MmAllocateContigiousMemory(
    IN  size_t NumberOfBytes,
    IN  uint64_t HighestAcceptableAddress
)

/*++

    Routine description:

        Allocate contingious physical memory pages and maps them. (used for DMA)

    Arguments:

        [IN]    size_t NumberOfBytes - The amount of contigious bytes to allocate.
        [IN]    uint64_t HighestAcceptableAddress - The highest physical address to find contigious bytes for. (used for drivers that cannot see the full 64bit system memory amount)

    Return Values:

        Base virtual address to allocated memory, or NULL on failure.

    Notes:

        This will probably cause fragmentation, and is very expensive as it iterates O(n) over the PFN Database, use sparingly.

--*/

{
    // According to MSDN this must be satisfied (this isnt NT compatible, but it follows its rules)
    if (MeGetCurrentIrql() > DISPATCH_LEVEL) return NULL;

    // Declarations
    size_t pageCount = BYTES_TO_PAGES(NumberOfBytes);
    PAGE_INDEX MaxPfn = PPFN_TO_INDEX(PHYSICAL_TO_PPFN(HighestAcceptableAddress));
    size_t ConsecutiveFound = 0;
    IRQL DbIrql;
    PAGE_INDEX StartIndex = 0;
    void* BaseAddress = NULL; // Null initially, unless enough pages.

    /* FIXME NonPagedPoolCacheAligned type. (That returns an addres that is page aligned actually), since ahci wanted & 0x3FF, for alignment.
    // First, try to allocate from the NonPagedPool, if it returned a contigious physical memory address, we are lucky! (if we reach MiRefillPool we are less lucky, its actually worse...)
    if (HighestAcceptableAddress == UINT64_T_MAX) {
        BaseAddress = MmAllocatePoolWithTag(NonPagedPool, NumberOfBytes, 'mCmM');

        if (BaseAddress) {
            if (MiCheckForContigiousMemory(BaseAddress, NumberOfBytes)) {
                // Its physically contigious!
                return BaseAddress;
            }
            else {
                // It's not.. Free allocated memory.
                MmFreePool(BaseAddress);
                BaseAddress = NULL;
            }
        }
    }
    */

    // Acquire the global DB lock so we dont get the contigious pages stolen from us.
    MsAcquireSpinlock(&PfnDatabase.PfnDatabaseLock, &DbIrql);

    for (PAGE_INDEX i = 0; i < PfnDatabase.TotalPageCount; i++) {
        // Check bounds.
        if (i >= MaxPfn) break;

        PPFN_ENTRY pfn = &PfnDatabase.PfnEntries[i];

        // Is this page a candidate
        bool isCandidate = (pfn->State == PfnStateFree || pfn->State == PfnStateZeroed || pfn->State == PfnStateStandby);

        if (isCandidate) {
            if (ConsecutiveFound == 0) {
                StartIndex = i;
            }
            ConsecutiveFound++;
        }
        else {
            ConsecutiveFound = 0;
        }

        // Found a good enough block?
        if (ConsecutiveFound == pageCount) {
            // We found a range! Now we must claim them.
            bool first = true;
            for (PAGE_INDEX j = 0; j < pageCount; j++) {
                PPFN_ENTRY pageToClaim = &PfnDatabase.PfnEntries[StartIndex + j];

                // Remove from whatever list it is currently in
                MiUnlinkPageFromList(pageToClaim);

                // Mark as active
                pageToClaim->State = PfnStateActive;
                pageToClaim->RefCount = 1;
                pageToClaim->Flags = PFN_FLAG_LOCKED_FOR_IO;

                // Clear mapping info
                pageToClaim->Descriptor.Mapping.PteAddress = NULL;
                pageToClaim->Descriptor.Mapping.Vad = NULL;

                // Map the physical to the offset.
                uintptr_t phys = PPFN_TO_PHYSICAL_ADDRESS(pageToClaim);
                uintptr_t virt = (phys + PhysicalMemoryOffset);

                PMMPTE pte = MiGetPtePointer(virt);
                assert((pte) != NULL);

                // Set the return value to the first address.
                if (first) {
                    first = false;
                    BaseAddress = (void*)virt;
                }

                // Write through is set, we want immediate flush to main memory.
                MI_WRITE_PTE(pte, virt, phys, PAGE_PRESENT | PAGE_RW | PAGE_PWT);
            }
            InterlockedAddU64(&PfnDatabase.TotalReserved, pageCount);
            // Break out of the 'i' loop
            break;
        }
    }

    MsReleaseSpinlock(&PfnDatabase.PfnDatabaseLock, DbIrql);
    // This could be NULL if we didnt find a contigious amount, or the valid pointer to start of block (mapped with PhysicalMemoryOffset)
    return BaseAddress;
}

void
MmFreeContigiousMemory(
    IN  void* BaseAddress,
    IN  size_t NumberOfBytes
)

/*++

    Routine description:

        Releases contigious physical memory allocated by the MmAllocateContigiousMemory routine.

    Arguments:

        [IN]    void* BaseAddress - Base virtual address to allocated memory, returned by the allocation routine.
        [IN]    size_t NumberOfBytes - Number of bytes allocated.

    Return Values:

        None.

--*/

{
    // Declarations
    IRQL DbIrql;
    size_t pageCount = BYTES_TO_PAGES(NumberOfBytes);
    uintptr_t CurrentAddress = (uintptr_t)BaseAddress;

    // Check if the base address is from the NPG Pool allocation.
    if (BaseAddress >= (void*)MI_NONPAGED_POOL_BASE && BaseAddress <= (void*)MI_NONPAGED_POOL_END) {
        MmFreePool(BaseAddress);
        return;
    }

    // Just unmap each page, and return the PFN to DB.
    MsAcquireSpinlock(&PfnDatabase.PfnDatabaseLock, &DbIrql);

    for (size_t i = 0; i < pageCount; i++) {
        // Retrieve the PTE for the current VA.
        PMMPTE pte = MiGetPtePointer(CurrentAddress);
        if (!pte) break;
        // Retrieve the PFN for the current PTE.
        PAGE_INDEX pfn = MiTranslatePteToPfn(pte);
        // Unmap the PTE.
        MiUnmapPte(pte);
        // Release the PFN back.
        MiReleasePhysicalPage(pfn);

        // Advance VA by VirtualPageSize
        CurrentAddress += VirtualPageSize;
    }

    MsReleaseSpinlock(&PfnDatabase.PfnDatabaseLock, DbIrql);
}

void*
MmMapIoSpace(
    IN uintptr_t PhysicalAddress,
    IN size_t NumberOfBytes,
    IN MEMORY_CACHING_TYPE CacheType
)

/*++

    Routine description:

        Maps the given physical address + NumberOfBytes to nonpaged system space.

    Arguments:

        [IN]    uintptr_t PhysicalAddress - Specifies the starting physical address of the I/O range to be mapped.
        [IN]    size_t NumberOfBytes - Specifies a value greater than zero, indicating the number of bytes to be mapped.
        [IN]    MEMORY_CACHING_TYPE CacheType - Specifies the cache attribute to use to map the physical address range.

    Return Values:

        Base Virtual Address that is mapped to the base physical address, or NULL on failure.

--*/

{
    // Declarations
    void* BaseAddress = NULL;
    size_t NumberOfPages = BYTES_TO_PAGES(NumberOfBytes);
    uint64_t CacheFlags = MiCacheToFlags(CacheType);

    // Runtime Assertions
    assert(NumberOfBytes > 0);
    assert(MeGetCurrentIrql() <= DISPATCH_LEVEL);

    // Get space reservation for amount of bytes. (we could also use PhysicalMemoryOffset, but the caller must adhere that the PhysicalAddress given is NOT mapped.)
    uintptr_t VA = MiAllocatePoolVa(NonPagedPool, NumberOfBytes);
    if (!VA) return NULL;

    // Good, now all we do is map, easy as that.
    uintptr_t CurrentVA = VA;
    uintptr_t CurrentPhys = PhysicalAddress;
    for (size_t i = 0; i < NumberOfPages; i++) {
        PMMPTE pte = MiGetPtePointer(CurrentVA);
        assert(pte != NULL);
        if (!pte) goto failure;

        // Write the PTE with the appropriate cache flags (requires PAT, enabled in MmInitSystem)
        MI_WRITE_PTE(pte, CurrentVA, CurrentPhys, PAGE_PRESENT | PAGE_RW | CacheFlags);

        // Advance the current addresses.
        CurrentPhys += PhysicalFrameSize;
        CurrentVA += VirtualPageSize;
    }

    BaseAddress = (void*)VA;
    return BaseAddress;

failure:
    if (VA) {
        MiFreePoolVaContiguous(VA, NumberOfBytes, NonPagedPool);
    }

    return NULL;
}
