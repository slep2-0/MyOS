/*++

Module Name:

    pool.c

Purpose:

    This translation unit contains the implementation of pool allocations in the kernel.

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "../../includes/mm.h"
#include "../../includes/me.h"
#include "../../assert.h"

// Can hold any size.
POOL_DESCRIPTOR GlobalPool;

#define POOL_TYPE_GLOBAL 9999
#define POOL_TYPE_PAGED  1234

MTSTATUS
MiInitializePoolSystem(
    void
)

/*++

    Routine description:

        Refills the specified pool with a block of its size.

    Arguments:

        None.

    Return Values:

        MTSTATUS Status Code.

--*/

{
    PPROCESSOR cpu = MeGetCurrentProcessor();
    if (!cpu) return MT_NOT_FOUND;

    size_t base = 32; // Start size
    for (int i = 0; i < MAX_POOL_DESCRIPTORS; i++) {
        PPOOL_DESCRIPTOR desc = &cpu->LookasidePools[i];

        // Would grow in binary (32,64,128,256...) (max would be 2048)
        size_t blockSize = (base << i) + sizeof(POOL_HEADER);
        desc->BlockSize = blockSize;
        desc->FreeCount = 0;
        desc->FreeListHead.Next = NULL;
        desc->TotalBlocks = 0;
        desc->PoolLock.locked = 0;
    }

    return MT_SUCCESS;
}

static
bool
MiRefillPool(
    PPOOL_DESCRIPTOR Desc,
    size_t PoolIndex
)

/*++

    Routine description:

        Refills the specified pool with a block of its size.

    Arguments:

        [IN]    PPOOL_DESCRIPTOR Desc - Pointer to descriptor.
        [IN]    size_T PoolIndex - Index of the slab in the CPU Lookaside buffer.

    Return Values:

        True or False based if allocation and/or refill succeeded.

--*/

{
    // Before allocating a va or another PFN, lets check the global pool first, see if we have a free 4KiB block.
    IRQL oldIrql;
    uintptr_t PageVa = 0;
    size_t HeaderBlockSize = 0;
    size_t Iterations = 0;

    // Acquire the spinlock for atomicity.
    MsAcquireSpinlock(&GlobalPool.PoolLock, &oldIrql);

    // Initialize the local list so that we push back to not memory leak blocks from the global pol.
    SINGLE_LINKED_LIST localList;
    localList.Next = NULL;

    while (GlobalPool.FreeCount) {
        // As long as we have a free block in the global pool, we check it.
        PSINGLE_LINKED_LIST list = GlobalPool.FreeListHead.Next;
        if (list == NULL) break; // FreeCount was wrong, but that's ok
        GlobalPool.FreeListHead.Next = list->Next;
        PPOOL_HEADER header = CONTAINING_RECORD(list, POOL_HEADER, Metadata.FreeListEntry);
        GlobalPool.FreeCount--;
        
        if (header->PoolCanary != 'BEKA') {
            MeBugCheckEx(
                MEMORY_CORRUPT_HEADER,
                (void*)header,
                (void*)__read_rip(),
                NULL,
                NULL
            );
        }

        if (Desc->BlockSize > header->Metadata.BlockSize) {
            // If the block gotten from the global pool is smaller than the required refill size, we continue and add this block to the list (to push back later)
            header->Metadata.FreeListEntry.Next = localList.Next;
            localList.Next = &header->Metadata.FreeListEntry;
            Iterations++;
            continue;
        }

        // The block is good! The loop that refills the desc wil overwrite this header data. (this is why we dont add sizeof)
        PageVa = (uintptr_t)header;
        HeaderBlockSize = header->Metadata.BlockSize;
        break;
    }

    // Refill back the pool.
    while (Iterations--) {
        PSINGLE_LINKED_LIST entryToPushBack = localList.Next;
        if (entryToPushBack == NULL) {
            // Shouldn't happen if iterations is correct, but I always admire checking.
            break;
        }

        localList.Next = entryToPushBack->Next;
        entryToPushBack->Next = GlobalPool.FreeListHead.Next;
        GlobalPool.FreeListHead.Next = entryToPushBack;

        GlobalPool.FreeCount++;
    }

    MsReleaseSpinlock(&GlobalPool.PoolLock, oldIrql);

    if (!PageVa) {
        // The global pool is empty... lets allocate.
        // Allocate a 4KiB virtual address.
        PageVa = MiAllocatePoolVa(NonPagedPool, VirtualPageSize);
        if (!PageVa) return false; // Out of VA Space.

        // Allocate a 4KiB Physical page.
        PAGE_INDEX pfn = MiRequestPhysicalPage(PfnStateZeroed);
        if (pfn == PFN_ERROR) {
            MiFreePoolVaContiguous(PageVa, VirtualPageSize, NonPagedPool);
            return false;
        }

        // Map the page permanently.
        PMMPTE pte = MiGetPtePointer((uintptr_t)PageVa);
        uint64_t phys = PPFN_TO_PHYSICAL_ADDRESS(INDEX_TO_PPFN(pfn));
        MI_WRITE_PTE(pte, PageVa, phys, PAGE_PRESENT | PAGE_RW);

        // Update PFN metadata.
        PPFN_ENTRY ppfn = INDEX_TO_PPFN(pfn);
        ppfn->State = PfnStateActive;
        ppfn->Flags = PFN_FLAG_NONPAGED;
        ppfn->Descriptor.Mapping.PteAddress = pte;
        ppfn->Descriptor.Mapping.Vad = NULL;
        HeaderBlockSize = VirtualPageSize;
    }

    // Reaching here means we got a valid page, now we must carve it up to the appropriate slab's size.
    // Acquire the spinlock for the descriptor given before modifying its data.
    IRQL descIrql;
    MsAcquireSpinlock(&Desc->PoolLock, &descIrql);

    // Loop from the start to the end of the page, stepping by the small block size.
    for (size_t offset = 0; (offset + Desc->BlockSize) <= HeaderBlockSize; offset += Desc->BlockSize) {
        // newBlock points to the start of this Desc->BlockSize chunk.
        PPOOL_HEADER newBlock = (PPOOL_HEADER)((uint8_t*)PageVa + offset);

        // Set its header metadata.
        newBlock->Metadata.BlockSize = Desc->BlockSize;
        newBlock->Metadata.PoolIndex = PoolIndex;
        newBlock->PoolCanary = 'BEKA'; // Pool Canary
        newBlock->PoolTag = 'ADIR'; // Default Tag
        
        // Add this block to the list of the descriptor.
        newBlock->Metadata.FreeListEntry.Next = Desc->FreeListHead.Next;
        Desc->FreeListHead.Next = &newBlock->Metadata.FreeListEntry;
        Desc->TotalBlocks++;
        Desc->FreeCount++;
    }

    MsReleaseSpinlock(&Desc->PoolLock, descIrql);
    return true;
}

static
void*
MiAllocateLargePool(
    size_t NumberOfBytes,
    uint32_t Tag
)

/*++

    Routine description:

        Allocates a NonPagedPool pool, and returns a pointer to start of region.

    Arguments:

        [IN]    size_t NumberOfBytes - Number of bytes needed to allocate.
        [IN]    uint32_t Tag - A 4 byte integer that signifies the current allocation, in little endian (e.g 'TSET' -> 'TEST')

    Return Values:

        Pointer to start of allocated region

--*/

{
    // Large pool allocations are always popped / pushed into the global free list (since its the only one that holds more than 2048 bytes)
    IRQL oldIrql;
    MsAcquireSpinlock(&GlobalPool.PoolLock, &oldIrql);

    // Compute the actual allocation size in bytes.
    size_t RequiredSize = NumberOfBytes + sizeof(POOL_HEADER);

    PSINGLE_LINKED_LIST* PtrToPrevNext = &GlobalPool.FreeListHead.Next;
    PSINGLE_LINKED_LIST list = GlobalPool.FreeListHead.Next;
    PPOOL_HEADER foundHeader = NULL;

    while (list) {
        PPOOL_HEADER header = CONTAINING_RECORD(list, POOL_HEADER, Metadata.FreeListEntry);

        if (header->PoolCanary != 'BEKA') {
            MeBugCheckEx(
                MEMORY_CORRUPT_HEADER,
                (void*)header,
                (void*)__read_rip(),
                NULL,
                NULL
            );
        }

        if (header->Metadata.BlockSize >= RequiredSize) {
            // Found a block that holds the amount of bytes we need!
            foundHeader = header;
            
            // Unlink it from the list.
            *PtrToPrevNext = list->Next;
            GlobalPool.FreeCount--;
            break;
        }

        // Didn't find an appropriate block, move to next.
        PtrToPrevNext = &list->Next;
        list = list->Next;
    }

    // Release the lock.
    MsReleaseSpinlock(&GlobalPool.PoolLock, oldIrql);

    // Now lets check if we found a block or we didn't.
    if (foundHeader) {
        // Good, lets set metadata, and return it to the caller.
        foundHeader->PoolTag = Tag;
        return (void*)((uint8_t*)foundHeader + sizeof(POOL_HEADER));
    }

    // Looks like we didn't find a block that has the amount of bytes we need, allocate one.
    size_t neededPages = BYTES_TO_PAGES(RequiredSize);

    // Allocate contiguous VAs.
    uintptr_t pageVa = MiAllocatePoolVa(NonPagedPool, RequiredSize);
    
    if (!pageVa) {
        // We don't have enough VA space to allocate required pool.
        return NULL;
    }

    // Now lets loop to request PFNs, we also create a safeguard to unroll the loop if failure happens.
    bool failure = false;
    size_t Iterations = 0;

    for (size_t i = 0; i < neededPages; i++) {
        // Increment by 4KiB each time.
        uint8_t* currVa = (uint8_t*)pageVa + (i * VirtualPageSize);

        PAGE_INDEX pfn = MiRequestPhysicalPage(PfnStateFree);

        if (pfn == PFN_ERROR) {
            // Allocation for a physical page failed, free the VA allocated, and unroll the loop (see code below loop)
            MiFreePoolVaContiguous(pageVa, RequiredSize, NonPagedPool);
            failure = true;
            break;
        }

        // Map the page.
        PMMPTE pte = MiGetPtePointer((uintptr_t)currVa);
        uint64_t phys = PPFN_TO_PHYSICAL_ADDRESS(INDEX_TO_PPFN(pfn));
        MI_WRITE_PTE(pte, currVa, phys, PAGE_PRESENT | PAGE_RW);
        
        // Update PFN metadata.
        PPFN_ENTRY ppfn = INDEX_TO_PPFN(pfn);
        ppfn->State = PfnStateActive;
        ppfn->Flags = PFN_FLAG_NONPAGED;
        ppfn->Descriptor.Mapping.PteAddress = pte;

        // Update iterations for loop unroll (if failure)
        Iterations++;
    }

    // If failure, unroll PFNs using Iterations.
    if (failure) {
        for (size_t j = 0; j < Iterations; j++) {
            uint8_t* vaToFree = (uint8_t*)pageVa + (j * VirtualPageSize);
            PMMPTE pte = MiGetPtePointer((uintptr_t)vaToFree);
            PAGE_INDEX pfn = MiTranslatePteToPfn(pte);
            MiUnmapPte(pte);
            MiReleasePhysicalPage(pfn);
        }
        return NULL;
    }

    // Success! Initialize the block and return the pointer to caller.
    PPOOL_HEADER newHeader = (PPOOL_HEADER)pageVa;
    newHeader->PoolCanary = 'BEKA';
    newHeader->PoolTag = Tag;
    newHeader->Metadata.BlockSize = neededPages * VirtualPageSize; // Store allocated size.
    newHeader->Metadata.PoolIndex = POOL_TYPE_GLOBAL;

    return (void*)((uint8_t*)newHeader + sizeof(POOL_HEADER));
}

static
void*
MiAllocatePagedPool(
    IN  size_t NumberOfBytes,
    IN  uint32_t Tag
)

/*++

    Routine description:

        Allocates a paged pool.

    Arguments:

        [IN]    size_t NumberOfBytes - Number of bytes needed to allocate.
        [IN]    uint32_t Tag - A 4 byte integer that signifies the current allocation, in big endian (e.g 'TSET' - "TEST")

    Return Values:

        Pointer to allocated region, or NULL on failure.

    Notes:

        This function AND access to it's pool contents MUST be with IRQL < DISPATCH_LEVEL.

--*/

{
    assert((MeGetCurrentIrql()) < DISPATCH_LEVEL, "IRQL Is dispatch or above at blocking function.");
    size_t ActualSize = NumberOfBytes + sizeof(POOL_HEADER);
    uintptr_t PagedVa = MiAllocatePoolVa(PagedPool, ActualSize);

    // Since we supplied the PagedPool parameter to MiAllocatePoolVa, it would internally use the MmAllocateVirtualMemory, now we are destined to page fault when we access PagedVa.
    // But IRQL restrictions guranteed us that we wont bugcheck now (as we are below Dispatch)
    PPOOL_HEADER header = (PPOOL_HEADER)PagedVa;
    
    // Set metadata.
    header->PoolCanary = 'BEKA';
    header->PoolTag = Tag;
    header->Metadata.BlockSize = ActualSize;
    header->Metadata.PoolIndex = POOL_TYPE_PAGED;

    // Return VA.
    return (void*)((uint8_t*)PagedVa + sizeof(POOL_HEADER));
}

void*
MmAllocatePoolWithTag(
    IN enum _POOL_TYPE PoolType,
    IN  size_t NumberOfBytes,
    IN  uint32_t Tag
)

/*++

    Routine description:

        Allocates a pool block of the specified type and returns a pointer to allocated block.

    Arguments:

        [IN]    enum _POOL_TYPE - POOL_TYPE Enumerator, specifying the type of pool that will be allocated.
        [IN]    size_t NumberOfBytes - Number of bytes needed to allocate.
        [IN]    uint32_t Tag - A 4 byte integer that signifies the current allocation, in big endian (e.g 'TSET' - "TEST")

    Return Values:

        Pointer to allocated region, or NULL on failure.

--*/

{
    // Declarations
    IRQL oldIrql;
    size_t ActualSize;
    PPROCESSOR cpu;
    PPOOL_DESCRIPTOR Desc;
    PPOOL_HEADER header;
    PSINGLE_LINKED_LIST list;
    size_t Index;

    // Runtime assertions
    assert((NumberOfBytes) != 0);
    assert((Tag) != 0);

    IRQL currIrql = MeGetCurrentIrql();

    // IRQL Must be less or equal to DISPATCH_LEVEL if allocating with NonPagedPool.
    // IRQL Must be LESS than DISPATCH_LEVEL if allocating with PagedPool.
    if (currIrql <= DISPATCH_LEVEL) {
        if (PoolType == PagedPool && currIrql == DISPATCH_LEVEL) {
            MeBugCheckEx(
                IRQL_NOT_LESS_OR_EQUAL,
                (void*)&MmAllocatePoolWithTag,
                (void*)MeGetCurrentIrql(),
                (void*)8,
                (void*)__builtin_return_address(0)
            );
        }
    }
    // IRQL Must NOT be greater than DISPATCH_LEVEL at any allocation.
    else {
        MeBugCheckEx(
            IRQL_NOT_LESS_OR_EQUAL,
            (void*)&MmAllocatePoolWithTag,
            (void*)MeGetCurrentIrql(),
            (void*)8,
            (void*)__builtin_return_address(0)
        );
    }

    if (PoolType == PagedPool) {
        // Use the internal paged pool allocator.
        return MiAllocatePagedPool(NumberOfBytes, Tag);
    }

    ActualSize = NumberOfBytes + sizeof(POOL_HEADER);
    cpu = MeGetCurrentProcessor();


    // It's NonPagedPool. Find the correct slab.
    Desc = NULL;
    for (int i = 0; i < MAX_POOL_DESCRIPTORS; i++) {
        PPOOL_DESCRIPTOR currentSlab = &cpu->LookasidePools[i];
        if (ActualSize <= currentSlab->BlockSize) {
            Desc = currentSlab;
            Index = i;
            break; // Found the best-fit slab
        }
    }

    if (Desc == NULL) {
        // Allocation is larger than 2048 bytes, use the large pool allocator.
        return MiAllocateLargePool(NumberOfBytes, Tag);
    }
    
    MsAcquireSpinlock(&Desc->PoolLock, &oldIrql);
    assert((Desc->FreeCount) != UINT64_T_MAX);

    if (Desc->FreeCount == 0) {
        // Looks like the pool is empty, refill all empty pools.
        // First, release the spinlock.
        MsReleaseSpinlock(&Desc->PoolLock, oldIrql);
        if (!MiRefillPool(Desc, Index)) {
            // If we failed allocation, act on failure.
            return NULL;
        }
        // Retry allocation.
        return MmAllocatePoolWithTag(PoolType, NumberOfBytes, Tag);
    }

    // Looks like we have a block to return! Return its PTR.
    // First, acquire it. (we are under spinlock, no need for interlocked pop)
    list = Desc->FreeListHead.Next;
    assert((list) != NULL, "Pool is nullptr even though freecount isn't zero.");
    Desc->FreeListHead.Next = list->Next; // Finish the pop
    header = CONTAINING_RECORD(list, POOL_HEADER, Metadata.FreeListEntry);

    // We must restore the metadata because the linked list pointer 
    // overwrote it while the block was sitting in the free list.
    header->Metadata.PoolIndex = (uint16_t)Index;
    header->Metadata.BlockSize = (uint16_t)Desc->BlockSize;

    // First check if the canary is wrong.
    if (header->PoolCanary != 'BEKA') {
        MeBugCheckEx(
            MEMORY_CORRUPT_HEADER,
            (void*)header,
            (void*)__read_rip(),
            NULL,
            NULL
        );
    }

    // Rewrite its tag.
    header->PoolTag = Tag;
    // Decrement descriptor free count.
    Desc->FreeCount--;
    assert((Desc->FreeCount) != SIZE_T_MAX); // Check for underflow.
    // Release spinlock.
    MsReleaseSpinlock(&Desc->PoolLock, oldIrql);
    // Return the pointer (exclude metadata start).
    return (void*)((uint8_t*)header + sizeof(POOL_HEADER));
}

void
MmFreePool(
    IN  void* buf
)

/*++

    Routine description:

        Deallocates the buffer allocated by the MmAllocatePoolWithTag routine (or other pool allocation routines if present).

    Arguments:

        [IN]    void* buf - The pointer given by the routine. (start of allocated region)

    Return Values:

        None.

    Notes:

        Memory is freed here, do not use the pointer after freeing the pool.

--*/

{
    if (!buf) return;

    // Convert the buffer to the header.
    PPOOL_HEADER header = (PPOOL_HEADER)((uint8_t*)buf - sizeof(POOL_HEADER));

    if (header->PoolCanary != 'BEKA') {
        MeBugCheckEx(
            MEMORY_CORRUPT_HEADER,
            (void*)header,
            (void*)__read_rip(),
            NULL,
            NULL
        );
    }

    // Obtain the pool index to free the region back into.
    uint16_t PoolIndex = header->Metadata.PoolIndex;

    if (PoolIndex == POOL_TYPE_GLOBAL) {
        // Big pool allocation, return it to the global pool.
        IRQL oldIrql;
        MsAcquireSpinlock(&GlobalPool.PoolLock, &oldIrql);

        // Set pool tag for clarity.
        header->PoolTag = 'ADIR';

        // Push the block back onto the global free list
        header->Metadata.FreeListEntry.Next = GlobalPool.FreeListHead.Next;
        GlobalPool.FreeListHead.Next = &header->Metadata.FreeListEntry;
        GlobalPool.FreeCount++;

        MsReleaseSpinlock(&GlobalPool.PoolLock, oldIrql);
        return;
    }

    if (PoolIndex == POOL_TYPE_PAGED) {
        // Paged pool allocation, we don't return it to any pool, instead, we free the VADs.
        return MiFreePoolVaContiguous((uintptr_t)header, header->Metadata.BlockSize, PagedPool);
    }

    //
    // Nonpaged pool allocation
    //

    PPROCESSOR cpu = MeGetCurrentProcessor();
    PPOOL_DESCRIPTOR Desc = &cpu->LookasidePools[PoolIndex];

    // Acquire the same lock used by the allocator
    IRQL oldIrql;
    MsAcquireSpinlock(&Desc->PoolLock, &oldIrql);

    // Push the entry back onto the list (it's protected by the lock)
    header->Metadata.FreeListEntry.Next = Desc->FreeListHead.Next;
    Desc->FreeListHead.Next = &header->Metadata.FreeListEntry;

    // Increment the free count.
    Desc->FreeCount++;

    // Release the lock
    MsReleaseSpinlock(&Desc->PoolLock, oldIrql);
}