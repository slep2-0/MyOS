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

MM_PFN_DATABASE PfnDatabase;

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

    // Calculate the number of entries in the memory map
    size_t entry_count = boot_info->MapSize / boot_info->DescriptorSize;

    // Get a pointer to the first descriptor
    PEFI_MEMORY_DESCRIPTOR desc = boot_info->MemoryMap;

    for (size_t i = 0; i < entry_count; i++) {
        // Calculate the end address of the current memory region.
        uint64_t region_end = desc->PhysicalStart + (desc->NumberOfPages * PhysicalFrameSize);

        // If this region ends at a higher address, update our maximum.
        if (region_end > highest_addr) highest_addr = region_end;

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

extern MMPTE HyperspacePtes[];

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
    desc = BootInfo->MemoryMap; // reset desc to original pointer
    for (size_t i = 0; i < entryCount; i++) {
        uint64_t regionStart = desc->PhysicalStart;
        uint64_t regionPages = desc->NumberOfPages;

        // For each 4KiB page in the physical region of pages
        for (uint64_t p = 0; p < regionPages; p++) {
            // The physical address is calculated by taking the (regionStart (physical address of region base) plus the current increment) times the frame size.
            uint64_t physAddr = regionStart + p * PhysicalFrameSize;

            uint64_t currentPfnIndex = (physAddr / PhysicalFrameSize);
            if (currentPfnIndex >= PfnDatabase.TotalPageCount) {
                // out of range physical address, we skip.
                continue;
            }

            PPFN_ENTRY entry = &PfnDatabase.PfnEntries[currentPfnIndex];

            // Initialize the PFN Entry.
            entry->RefCount = 0;

            switch (desc->Type) {
            case EfiConventionalMemory:
                // If this page is inside the PFN-array region we just reserved, skip adding it to free
                if (physAddr >= pfn_region_phys && physAddr < pfn_region_phys + neededPages * VirtualPageSize) {
                    break;
                }
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
    // Return NULL if list is empty.
    if (ListEntry->Flink == ListEntry->Blink) return NULL;
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

        PFN Index of the page, otherwise 0 on failure.

    Notes:

        The PFN index given, does not return an actively mapped PFN (that is mapped to a VA), other functions must set its mapping.

--*/

{  
    // Declarations
    IRQL oldIrql;
    PPFN_ENTRY pfn = NULL;
    PFN_STATE oldState; // To know if we need to zero

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
    // TODO: Paging
    return 0;

found:
    // Decrement total available pages
    InterlockedDecrementU64(&PfnDatabase.AvailablePages);

    assert((pfn->RefCount) == 0);
    uint64_t pfnIndex = PPFN_TO_INDEX(pfn);

    // Set state to Transition (locked)
    pfn->State = PfnStateTransition;

    // If caller wants a zeroed page, but we didn't get one, zero it now.
    if (ListType == PfnStateZeroed && oldState != PfnStateZeroed) {
        IRQL hyperIrql;
        uint8_t* va = MiMapPageInHyperspace(pfnIndex, &hyperIrql);
        kmemset(va, 0, VirtualPageSize);
        MiUnmapHyperSpaceMap(hyperIrql);
    }

    // Set final metadata: now "owned" by the caller.
    pfn->RefCount = 1;
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

    if (InterlockedDecrementU64(&pfn->RefCount) == 0) {
        // This is the last reference to the page, store it back in the list.
        if (pfn->State == PfnStateActive) {
            // Clear mapping info.
            pfn->Descriptor.Mapping.Vad = NULL;
            if (pfn->Descriptor.Mapping.PteAddress->PresentSet.Dirty == true) {
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