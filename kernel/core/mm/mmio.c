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

    uintptr_t PageOffset = VA_OFFSET(StartAddress);
    if (NumberOfBytes > SIZE_MAX - PageOffset) return false;
    size_t AmtPages = BYTES_TO_PAGES(NumberOfBytes + PageOffset);
    uintptr_t CurrentAddress = (uintptr_t)StartAddress;

    // Get the First PFN.
    PMMPTE CurrentPte = MiGetPtePointer(CurrentAddress);

    // Check if PTE exists and is valid before translating
    if (!CurrentPte || !CurrentPte->Hard.Present) return false;

    PAGE_INDEX StartPfn = MiTranslatePteToPfn(CurrentPte);
    if (StartPfn == PFN_ERROR) return false;

    // Loop from i = 1 (we already checked the first page)
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
    if (MeGetCurrentIrql() > APC_LEVEL) return NULL;
    if (NumberOfBytes == 0 ||
        NumberOfBytes > SIZE_MAX - (PhysicalFrameSize - 1) ||
        PfnDatabase.TotalPageCount == 0) {
        return NULL;
    }

    // Declarations
    size_t pageCount = BYTES_TO_PAGES(NumberOfBytes);
    uint64_t AddressablePages = HighestAcceptableAddress == UINT64_MAX
        ? (UINT64_MAX / PhysicalFrameSize) + 1
        : (HighestAcceptableAddress + 1) / PhysicalFrameSize;
    PAGE_INDEX SearchLimit = (PAGE_INDEX)MIN(
        AddressablePages,
        (uint64_t)PfnDatabase.TotalPageCount
    );
    if (pageCount > SearchLimit) return NULL;

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

    // Claim the physical run while holding the database lock. Page-table
    // creation happens later because MiGetPtePointer can itself request a PFN.
    MsAcquireSpinlock(&PfnDatabase.PfnDatabaseLock, &DbIrql);

    for (PAGE_INDEX i = 0; i < SearchLimit; i++) {

        PPFN_ENTRY pfn = &PfnDatabase.PfnEntries[i];

        // Is this page a candidate
        // Standby pages still have a transition PTE owner. They cannot be
        // recycled until that PTE is invalidated through a real trim path.
        bool isCandidate = (pfn->State == PfnStateFree ||
            pfn->State == PfnStateZeroed);

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
            // We found a range. Remove every page from its availability list
            // and publish ownership before dropping the database lock.
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
            }
            BaseAddress = (void*)(PFN_TO_PHYS(StartIndex) + PhysicalMemoryOffset);
            InterlockedAddU64(&PfnDatabase.TotalReserved, pageCount);
            break;
        }
    }

    MsReleaseSpinlock(&PfnDatabase.PfnDatabaseLock, DbIrql);
    if (!BaseAddress) return NULL;

    size_t MappedPages = 0;
    for (; MappedPages < pageCount; MappedPages++) {
        PAGE_INDEX PfnIndex = StartIndex + MappedPages;
        uintptr_t Phys = PFN_TO_PHYS(PfnIndex);
        uintptr_t Virt = Phys + PhysicalMemoryOffset;
        PMMPTE Pte = MiGetPtePointer(Virt);
        if (!Pte) goto MappingFailure;

        // Write-through is used for DMA visibility.
        MI_WRITE_PTE(Pte, Virt, Phys, PAGE_PRESENT | PAGE_RW | PAGE_PWT);
        PfnDatabase.PfnEntries[PfnIndex].Flags =
            PFN_FLAG_NONPAGED | PFN_FLAG_LOCKED_FOR_IO;
    }

    return BaseAddress;

MappingFailure:
    for (size_t i = 0; i < pageCount; i++) {
        PAGE_INDEX PfnIndex = StartIndex + i;
        if (i < MappedPages) {
            uintptr_t Virt = PFN_TO_PHYS(PfnIndex) + PhysicalMemoryOffset;
            PMMPTE Pte = MiGetPtePointer(Virt);
            if (Pte && Pte->Hard.Present) MiUnmapPte(Pte);
        }
        MiReleasePhysicalPage(PfnIndex);
        InterlockedDecrementU64(&PfnDatabase.TotalReserved);
    }
    return NULL;
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
    if (!BaseAddress || NumberOfBytes == 0 ||
        NumberOfBytes > SIZE_MAX - (PhysicalFrameSize - 1) ||
        ((uintptr_t)BaseAddress & (VirtualPageSize - 1)) != 0) {
        return;
    }

    // Declarations
    size_t pageCount = BYTES_TO_PAGES(NumberOfBytes);
    uintptr_t CurrentAddress = (uintptr_t)BaseAddress;

    // Check if the base address is from the NPG Pool allocation.
    if (BaseAddress >= (void*)MI_NONPAGED_POOL_BASE && BaseAddress <= (void*)MI_NONPAGED_POOL_END) {
        MmFreePool(BaseAddress);
        return;
    }

    // Unmap each page, then return its PFN. Do not hold the database lock
    // across page-table walking; the walk may allocate page-table PFNs.
    for (size_t i = 0; i < pageCount; i++) {
        // Retrieve the PTE for the current VA.
        PMMPTE pte = MiGetPtePointer(CurrentAddress);
        if (!pte || !pte->Hard.Present) break;
        // Retrieve the PFN for the current PTE.
        PAGE_INDEX pfn = MiTranslatePteToPfn(pte);
        if (!MiIsValidPfn(pfn)) break;
        // Unmap the PTE.
        MiUnmapPte(pte);
        // Release the PFN back.
        MiReleasePhysicalPage(pfn);
        InterlockedDecrementU64(&PfnDatabase.TotalReserved);

        // Advance VA by VirtualPageSize
        CurrentAddress += VirtualPageSize;
    }

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
    // Runtime Assertions
    assert(NumberOfBytes > 0);
    assert(MeGetCurrentIrql() <= DISPATCH_LEVEL);
    if (NumberOfBytes == 0 ||
        PhysicalAddress > UINTPTR_MAX - (NumberOfBytes - 1)) {
        return NULL;
    }

    uintptr_t PageOffset = PhysicalAddress & (VirtualPageSize - 1);
    uintptr_t PhysicalBase = PhysicalAddress - PageOffset;
    if (NumberOfBytes > SIZE_MAX - PageOffset) return NULL;

    size_t SpannedBytes = NumberOfBytes + PageOffset;
    if (SpannedBytes > SIZE_MAX - (VirtualPageSize - 1)) return NULL;

    size_t MappingBytes = ALIGN_UP(SpannedBytes, VirtualPageSize);
    size_t NumberOfPages = MappingBytes / VirtualPageSize;
    uint64_t CacheFlags = MiCacheToFlags(CacheType);

    // Get space reservation for amount of bytes. (we could also use PhysicalMemoryOffset, but the caller must adhere that the PhysicalAddress given is NOT mapped, and I dont have time for their shenangians)
    uintptr_t VA = MiAllocatePoolVa(NonPagedPool, MappingBytes);
    if (!VA) return NULL;

    uintptr_t CurrentVA = VA;
    uintptr_t CurrentPhys = PhysicalBase;
    size_t MappedPages = 0;
    for (; MappedPages < NumberOfPages; MappedPages++) {
        PMMPTE pte = MiGetPtePointer(CurrentVA);
        assert(pte != NULL);
        if (!pte) goto failure;

        // Device addresses are not owned RAM PFNs. Publishing this mapping via
        // MI_WRITE_PTE would index the PFN database with a PCI/LAPIC address.
        uint64_t PteValue = (CurrentPhys & ~0xFFFULL) |
            PAGE_PRESENT | PAGE_RW | CacheFlags;
        MiAtomicExchangePte(pte, PteValue);
        invlpg((void*)CurrentVA);

        CurrentPhys += PhysicalFrameSize;
        CurrentVA += VirtualPageSize;
    }

    MiReloadTLBs();
    return (void*)(VA + PageOffset);

failure:
    for (size_t i = 0; i < MappedPages; i++) {
        uintptr_t MappedVa = VA + (i * VirtualPageSize);
        PMMPTE Pte = MiGetPtePointer(MappedVa);
        if (Pte) MiAtomicExchangePte(Pte, 0);
        invlpg((void*)MappedVa);
    }
    MiReloadTLBs();
    MiFreePoolVaContiguous(VA, MappingBytes, NonPagedPool);
    return NULL;
}

void
MmUnmapIoSpace(
    IN void* VirtualAddress,
    IN size_t NumberOfBytes
)

/*++

    Routine description:

        Unmaps the given physical address range by the kernel.

    Arguments:

        [IN]    void* VirtualAddress - The VirtualAddress given by the MmMapIoSpace routine.
        [IN]    size_t NumberOfBytes - The amount of bytes allocated by the kernel.

    Return Values:

        None.

--*/

{
    // The VirtualAddress given by the kernel is a retval of MiAllocatePoolVa
    // Haven't worked on the kernel in about 2 months, so I need a refresher.
    // Runtime Assertions
    assert(NumberOfBytes > 0);
    assert(MeGetCurrentIrql() <= DISPATCH_LEVEL);
    if (!VirtualAddress || NumberOfBytes == 0) return;

    uintptr_t ReturnedVa = (uintptr_t)VirtualAddress;
    uintptr_t PageOffset = ReturnedVa & (VirtualPageSize - 1);
    uintptr_t MappingBase = ReturnedVa - PageOffset;
    if (NumberOfBytes > SIZE_MAX - PageOffset) {
        MeBugCheckEx(BAD_POOL_CALLER, VirtualAddress,
            (void*)(uintptr_t)NumberOfBytes, NULL, RETADDR(0));
    }

    size_t SpannedBytes = NumberOfBytes + PageOffset;
    if (SpannedBytes > SIZE_MAX - (VirtualPageSize - 1)) {
        MeBugCheckEx(BAD_POOL_CALLER, VirtualAddress,
            (void*)(uintptr_t)NumberOfBytes, NULL, RETADDR(0));
    }

    size_t MappingBytes = ALIGN_UP(SpannedBytes, VirtualPageSize);
    size_t NumberOfPages = MappingBytes / VirtualPageSize;

    // Loop over the range given (by pages)
    for (size_t i = 0; i < NumberOfPages; i++) {
        uintptr_t CurrentVA = MappingBase + (i * VirtualPageSize);
        PMMPTE Pte = MiGetPtePointer(CurrentVA);
        assert(Pte != NULL);
        if (Pte) MiAtomicExchangePte(Pte, 0);
        invlpg((void*)CurrentVA);
    }

    // MMIO frames belong to the device/firmware, not to the PFN allocator.
    MiReloadTLBs();
    MiFreePoolVaContiguous(MappingBase, MappingBytes, NonPagedPool);
}
