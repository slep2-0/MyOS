/*++

Module Name:

    pfn.c

Purpose:

    This translation unit contains the implementation of the PFN Database (covers physical memory map init)

Author:

    slep (Matanel) 2025.

Revision History:
    DD/MM/YY


    17/10/2025 - Revised Physical Memory from a simple bitmap to a PFN database.
    

--*/

#include "../../includes/mm.h"
#include "../../includes/mg.h"
#include "../../assert.h"
#include "../../includes/me.h"

MM_PFN_DATABASE PfnDatabase;
bool MmPfnDatabaseInitialized = false;
PAGE_INDEX MmHighestPfn = 0;

static
uint64_t 
MiGetTotalMemory (
    const BOOT_INFO* boot_info
)

/*++

    Routine description:
    
        Calculates the total amount of physical memory in the system.

    Arguments:

        [IN]    Pointer to BOOT_INFO struct, obtained from UEFI.

    Return Values:

        Heighest Address in physical memory (total amount of RAM)

--*/

{
    uint64_t highest_addr = 0;
    size_t entry_count = boot_info->MapSize / boot_info->DescriptorSize;
    PEFI_MEMORY_DESCRIPTOR desc = boot_info->MemoryMap;

    for (size_t i = 0; i < entry_count; i++) {
        // FILTER: Only look at usable RAM or memory we might reclaim.
        // Ignore Reserved, Unusable, and MemoryMappedIO.
        // TODO Other types...
        if (desc->Type == EfiConventionalMemory)
        {
            uint64_t region_end = desc->PhysicalStart + (desc->NumberOfPages * PhysicalFrameSize);
            if (region_end > highest_addr) highest_addr = region_end;
        }

        desc = (PEFI_MEMORY_DESCRIPTOR)((uint8_t*)desc + boot_info->DescriptorSize);
    }

    return highest_addr;
}

static
void
MiReservePhysRange(
    uint64_t phys_start,
    uint64_t length
)

{
    uint64_t first = phys_start / PhysicalFrameSize;
    uint64_t pages = (length + PhysicalFrameSize - 1) / PhysicalFrameSize;
    for (uint64_t i = 0; i < pages; ++i) {
        uint64_t idx = first + i;
        if (idx >= PfnDatabase.TotalPageCount) continue;
        PPFN_ENTRY e = &PfnDatabase.PfnEntries[idx];
        e->RefCount = 1;
        e->State = PfnStateActive;
        e->Flags = PFN_FLAG_NONE;
        InterlockedIncrementU64(&PfnDatabase.TotalReserved);
    }
}

MTSTATUS
MiInitializePfnDatabase(
    IN  PBOOT_INFO BootInfo
)

/*++

    Routine description: 
        
        Initializes the global PFN database.

    Arguments:

        [IN]    Pointer to BOOT_INFO struct, obtained from UEFI.

    Return Values:

        MTSTATUS Status Code.

--*/

{
    // First of all, before creating the PFN Database
    // we would need the amount of total RAM, divide that by each physical frame size
    // to allocate the amount of PFN_ENTRY(ies) needed.
    uint64_t totalRam = MiGetTotalMemory(BootInfo);
    if (!totalRam) return MT_NO_MEMORY;

    uint64_t totalPfnEntries = totalRam / PhysicalFrameSize;

    // The amount of PFN Entries needed times the sizeof the struct, gives us the amount in bytes.
    uint64_t neededRam = totalPfnEntries * sizeof(PFN_ENTRY);
    assert((neededRam) < INT32_MAX, "Needed Ram DB is insanely huge");

    // Now, we need to find a suitable memory region to hold the PFN Entries in.
    PEFI_MEMORY_DESCRIPTOR desc = BootInfo->MemoryMap;
    size_t entryCount = BootInfo->MapSize / BootInfo->DescriptorSize;
    uint64_t pfnEntriesPhys = 0;

    // Loop over all memory.
    for (size_t i = 0; i < entryCount; i++) {
        if (desc->Type == EfiConventionalMemory) {
            // This physical region size is the number of pages in it times frame size.
            uint64_t regionSize = desc->NumberOfPages * PhysicalFrameSize;

            if (regionSize >= neededRam) {
                // This region can hold the PFN database entries.
                pfnEntriesPhys = desc->PhysicalStart;
                break;
            }
        }
        // Advance to the next UEFI memory descriptor in the map.
        desc = (PEFI_MEMORY_DESCRIPTOR)((uint8_t*)desc + BootInfo->DescriptorSize);
    }

    // Verify we found a suitable region.
    if (!pfnEntriesPhys) return MT_NOT_FOUND;

    // Convert the address to our virtual offsetable.
    uint64_t pfnEntriesVirt = pfnEntriesPhys + PhysicalMemoryOffset;

    // Initialize the doubly linked lists.
    InitializeListHead(&PfnDatabase.FreePageList.ListEntry);
    InitializeListHead(&PfnDatabase.BadPageList.ListEntry);
    InitializeListHead(&PfnDatabase.StandbyPageList.ListEntry);
    InitializeListHead(&PfnDatabase.ZeroedPageList.ListEntry);
    InitializeListHead(&PfnDatabase.ModifiedPageList.ListEntry);

    // Map the whole region, acquire its PTE for each 4KiB.
    uint64_t neededPages = (neededRam + VirtualPageSize - 1) / VirtualPageSize; 

    for (uint64_t i = 0; i < neededPages; i++) {
        PMMPTE pte = MiGetPtePointer(pfnEntriesVirt);
        if (!pte) return MT_GENERAL_FAILURE;
        MI_WRITE_PTE(pte, pfnEntriesVirt, pfnEntriesPhys, PAGE_PRESENT | PAGE_RW);
        pfnEntriesVirt += VirtualPageSize;
        pfnEntriesPhys += VirtualPageSize;
    }

    // Set the pointer.
    uint64_t pfn_region_phys = pfnEntriesPhys - (neededPages * VirtualPageSize);
    PfnDatabase.PfnEntries = (PPFN_ENTRY)(uintptr_t)(pfn_region_phys + PhysicalMemoryOffset);

    // Zero the region.
    kmemset(PfnDatabase.PfnEntries, 0, neededPages * VirtualPageSize);

    // Initialize counts.
    PfnDatabase.TotalPageCount = totalPfnEntries;            
    PfnDatabase.AvailablePages = 0;
    PfnDatabase.TotalReserved = 0;

    PfnDatabase.FreePageList.Count = 0;
    PfnDatabase.BadPageList.Count = 0;
    PfnDatabase.StandbyPageList.Count = 0;
    PfnDatabase.ZeroedPageList.Count = 0;
    PfnDatabase.ModifiedPageList.Count = 0;

    // Initialize locks
    PfnDatabase.PfnDatabaseLock.locked = 0;
    PfnDatabase.BadPageList.PfnListLock.locked = 0;
    PfnDatabase.StandbyPageList.PfnListLock.locked = 0;
    PfnDatabase.ZeroedPageList.PfnListLock.locked = 0;
    PfnDatabase.FreePageList.PfnListLock.locked = 0;
    PfnDatabase.ModifiedPageList.PfnListLock.locked = 0;

    // Reserve the PFN Array in the PFN List.
    MiReservePhysRange(pfn_region_phys, neededPages * VirtualPageSize); // implement helper below

    // We can interact with the entries, begin filling the PFN DB.
    PAGE_INDEX lastPfnIdx = 0;
    desc = BootInfo->MemoryMap; // reset desc to original pointer
    for (size_t i = 0; i < entryCount; i++) {
        uint64_t regionStart = desc->PhysicalStart;
        uint64_t regionPages = desc->NumberOfPages;

        // For each 4KiB page in the physical region of pages
        for (uint64_t p = 0; p < regionPages; p++) {
            // The physical address is calculated by taking the (regionStart (physical address of region base) plus the current increment) times the frame size.
            uint64_t physAddr = regionStart + p * PhysicalFrameSize;

            uint64_t currentPfnIndex = (physAddr / PhysicalFrameSize);

            if (currentPfnIndex > lastPfnIdx) {
                lastPfnIdx = currentPfnIndex;
            }

            if (currentPfnIndex >= PfnDatabase.TotalPageCount) {
                // out of range physical address, we skip.
                continue;
            }

            PPFN_ENTRY entry = &PfnDatabase.PfnEntries[currentPfnIndex];

            // If this page is inside the PFN-array region we just reserved, skip adding it to db.
            if (desc->Type == EfiConventionalMemory) {
                if (physAddr >= pfn_region_phys && physAddr < pfn_region_phys + neededPages * VirtualPageSize) {
                    continue; // Do not touch this entry, it was set by MiReservePhysRange
                }
            }

            // Initialize the PFN Entry.
            entry->RefCount = 0;

            switch (desc->Type) {
            case EfiConventionalMemory:
                entry->State = PfnStateFree;
                entry->Flags = PFN_FLAG_NONE;

                // Add to free list.
                InsertTailList(&PfnDatabase.FreePageList.ListEntry, &entry->Descriptor.ListEntry);
                // Increment the free page list count.
                InterlockedIncrementU64(&PfnDatabase.FreePageList.Count);
                InterlockedIncrementU64(&PfnDatabase.AvailablePages);
                break;
            case EfiBootServicesCode:
            case EfiBootServicesData:
            case EfiLoaderCode:
            case EfiLoaderData:
            case EfiRuntimeServicesCode:
            case EfiRuntimeServicesData:
            case EfiReservedMemoryType:
            case EfiACPIMemoryNVS:
                // Mark pages used by firmware, loader, or kernel as active.
                // these will not be returned by the page allocator.
                entry->State = PfnStateActive;
                entry->Flags = PFN_FLAG_NONE;
                entry->Descriptor.Mapping.PteAddress = NULL;
                entry->Descriptor.Mapping.Vad = NULL; // No VAD for these.
                entry->RefCount = 1; // Set reference count to 1, so allocator will not get confused.
                InterlockedIncrementU64(&PfnDatabase.TotalReserved);
                break;
            //case EfiACPIReclaimMemory TODO RECLAIMABLE.
            default:
                // If its not conventional memory, or one of the runtime/loader/boot/reserved, it is bad memory.
                entry->State = PfnStateBad;
                entry->Flags = PFN_FLAG_NONE;

                // Add to free list and increment count.
                InsertTailList(&PfnDatabase.BadPageList.ListEntry, &entry->Descriptor.ListEntry);
                InterlockedIncrementU64(&PfnDatabase.BadPageList.Count);
                break;
            }
        }
        desc = (PEFI_MEMORY_DESCRIPTOR)((uint8_t*)desc + BootInfo->DescriptorSize);
    }

    // Set the global state as initialized.
    MmPfnDatabaseInitialized = true;
    MmHighestPfn = lastPfnIdx;
    return MT_SUCCESS;
}

static
PPFN_ENTRY
MiReleaseAnyPage(
    IN PDOUBLY_LINKED_LIST ListEntry
)

/*++

    Routine description:

        Attempts to retrieve a PFN Entry from the ListEntry given.

    Arguments:

        [IN]    Pointer to ListEntry of PFN_LIST.

    Return Values:

        Pointer of PFN_ENTRY, NULL on failure.

--*/

{
    PDOUBLY_LINKED_LIST pListEntry = RemoveHeadList(ListEntry);
    if (!pListEntry) return NULL;

    // Return the PFN_ENTRY of this ListEntry.
    PPFN_ENTRY pPfnEntry = CONTAINING_RECORD(pListEntry, PFN_ENTRY, Descriptor.ListEntry);
    return pPfnEntry;
}

PAGE_INDEX
MiRequestPhysicalPage(
    IN  PFN_STATE ListType
)

/*++

    Routine description:

        Retrieves a physical page from the PFN Database.

    Arguments:

        [IN]    enum _PFN_STATE Type. (e.g PfnStateZeroed is guranteed to return a zeroed physical page)

    Return Values:

        PFN Index of the page, otherwise PFN_ERROR on failure.

    Notes:

        The PFN index given, does not return an actively mapped PFN (that is mapped to a VA), other functions must set its mapping.

--*/

{  
    // Declarations
    IRQL oldIrql;
    IRQL DbIrql;
    PPFN_ENTRY pfn = NULL;
    PFN_STATE oldState; // To know if we need to zero
    
    // Acquire global PFN DB lock.
    MsAcquireSpinlock(&PfnDatabase.PfnDatabaseLock, &DbIrql);
    
    // 1. Try ZeroedPageList
    MsAcquireSpinlock(&PfnDatabase.ZeroedPageList.PfnListLock, &oldIrql);
    pfn = MiReleaseAnyPage(&PfnDatabase.ZeroedPageList.ListEntry);
    MsReleaseSpinlock(&PfnDatabase.ZeroedPageList.PfnListLock, oldIrql);
    if (pfn) {
        InterlockedDecrementU64(&PfnDatabase.ZeroedPageList.Count);
        oldState = PfnStateZeroed;
        goto found;
    }

    // 2. Try FreePageList
    MsAcquireSpinlock(&PfnDatabase.FreePageList.PfnListLock, &oldIrql);
    pfn = MiReleaseAnyPage(&PfnDatabase.FreePageList.ListEntry);
    MsReleaseSpinlock(&PfnDatabase.FreePageList.PfnListLock, oldIrql);
    if (pfn) {
        InterlockedDecrementU64(&PfnDatabase.FreePageList.Count);
        oldState = PfnStateFree;
        goto found;
    }

    // 3. Try StandbyPageList
    MsAcquireSpinlock(&PfnDatabase.StandbyPageList.PfnListLock, &oldIrql);
    pfn = MiReleaseAnyPage(&PfnDatabase.StandbyPageList.ListEntry);
    MsReleaseSpinlock(&PfnDatabase.StandbyPageList.PfnListLock, oldIrql);
    if (pfn) {
        InterlockedDecrementU64(&PfnDatabase.StandbyPageList.Count);
        oldState = PfnStateStandby;
        goto found;
    }

    // 4. All lists are empty
    // TODO: Paging (flush modified list to disk, give a page from there.)
    // Release Global Lock
    MsReleaseSpinlock(&PfnDatabase.PfnDatabaseLock, DbIrql);
    return (uint64_t)-1;

found:
    // Claim while locked.
    assert((pfn->RefCount) == 0);
    pfn->State = PfnStateTransition;
    // Set final metadata: now "owned" by the caller.
    pfn->RefCount = 1;

    // Release Global Lock
    MsReleaseSpinlock(&PfnDatabase.PfnDatabaseLock, DbIrql);
    // Decrement total available pages
    InterlockedDecrementU64(&PfnDatabase.AvailablePages);

    uint64_t pfnIndex = PPFN_TO_INDEX(pfn);

    // If caller wants a zeroed page, but we didn't get one, zero it now.
    if (ListType == PfnStateZeroed && oldState != PfnStateZeroed) {
        IRQL hyperIrql;
        uint8_t* va = MiMapPageInHyperspace(pfnIndex, &hyperIrql);
        kmemset(va, 0, VirtualPageSize);
        MiUnmapHyperSpaceMap(hyperIrql);
    }

    return pfnIndex;
}

void
MiReleasePhysicalPage(
    IN  PAGE_INDEX PfnIndex
)

/*++

    Routine description:

        Releases a physical page back to the memory manager

    Arguments:

        [IN]    PAGE_INDEX Index of the PFN given by MiRequestPhysicalPage

    Return Values:

        None.

--*/

{
    // First, access the PFN in the database to determine its staistics.
    PPFN_ENTRY pfn = INDEX_TO_PPFN(PfnIndex);

    assert((pfn->RefCount) > 0, "Refcount is 0 while releasing. Double Free");

    if (InterlockedDecrementU32(&pfn->RefCount) == 0) {
        // This is the last reference to the page, store it back in the list.
        if (pfn->State == PfnStateActive) {
            // Clear mapping info.
            pfn->Descriptor.Mapping.Vad = NULL;
            if (pfn->Descriptor.Mapping.PteAddress != NULL &&
                pfn->Descriptor.Mapping.PteAddress->Hard.Dirty) {
                // Dirty bit is set, we throw it back to the modified page list.
                IRQL oldIrql;
                pfn->State = PfnStateModified;
                MsAcquireSpinlock(&PfnDatabase.ModifiedPageList.PfnListLock, &oldIrql);
                // TODO SET FILE OFFSET PAGING
                InsertTailList(&PfnDatabase.ModifiedPageList.ListEntry, &pfn->Descriptor.ListEntry);
                
                // Increment the counters
                InterlockedIncrementU64(&PfnDatabase.ModifiedPageList.Count);
                InterlockedIncrementU64(&PfnDatabase.AvailablePages);
                
                MsReleaseSpinlock(&PfnDatabase.ModifiedPageList.PfnListLock, oldIrql);
            }
            else {
                // Dirty bit is not set, we throw it to the standby list.
                IRQL oldIrql;
                pfn->State = PfnStateStandby;
                MsAcquireSpinlock(&PfnDatabase.StandbyPageList.PfnListLock, &oldIrql);
                // TODO SET FILE OFFSET PAGING
                InsertTailList(&PfnDatabase.StandbyPageList.ListEntry, &pfn->Descriptor.ListEntry);

                // Increment the counters
                InterlockedIncrementU64(&PfnDatabase.StandbyPageList.Count);
                InterlockedIncrementU64(&PfnDatabase.AvailablePages);

                MsReleaseSpinlock(&PfnDatabase.StandbyPageList.PfnListLock, oldIrql);
            }
        }
    }
}

static
void
MiUnlinkPageFromList(
    PPFN_ENTRY pfn
)
{
    IRQL oldIrql;
    SPINLOCK* lock = NULL;
    volatile uint64_t* count = NULL;

    /* Determine which list this PFN is on and pick the corresponding lock/count */
    switch (pfn->State) {
    case PfnStateFree:
        lock = &PfnDatabase.FreePageList.PfnListLock;
        count = &PfnDatabase.FreePageList.Count;
        break;
    case PfnStateZeroed:
        lock = &PfnDatabase.ZeroedPageList.PfnListLock;
        count = &PfnDatabase.ZeroedPageList.Count;
        break;
    case PfnStateStandby:
        lock = &PfnDatabase.StandbyPageList.PfnListLock;
        count = &PfnDatabase.StandbyPageList.Count;
        break;
    default:
        /* Active/Modified/Bad pages are handled elsewhere */
        return;
    }

    MsAcquireSpinlock(lock, &oldIrql);

    /*
     * Guard: if the entry isn't linked (both pointers NULL) then nothing to do.
     * This avoids calling RemoveEntryList on an unlinked node.
     */
    if (pfn->Descriptor.ListEntry.Flink == NULL &&
        pfn->Descriptor.ListEntry.Blink == NULL) {
        MsReleaseSpinlock(lock, oldIrql);
        return;
    }

    /* Remove this node from whatever list it currently sits on. */
    RemoveEntryList(&pfn->Descriptor.ListEntry);

    /* Clear the entry's links to mark it as unlinked (like RemoveHeadList does). */
    pfn->Descriptor.ListEntry.Flink = pfn->Descriptor.ListEntry.Blink = NULL;

    /* Update list and global counts while holding the lock. */
    InterlockedDecrementU64(count);
    InterlockedDecrementU64(&PfnDatabase.AvailablePages);

    MsReleaseSpinlock(lock, oldIrql);
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