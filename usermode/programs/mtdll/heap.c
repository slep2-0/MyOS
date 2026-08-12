/*++

Module Name:

    heap.c

Purpose:

    This translation unit contains the implementation of process heaps.

Author:

    slep (Matanel) 2026.

Notes:

    Small allocations (up to 4096 bytes) are serviced by size-class
    allocators. Requests are rounded to an appropriate bucket size
    (for example, 27 bytes -> 32-byte bucket, 100 bytes -> 128-byte
    bucket). Each bucket obtains slabs containing multiple fixed-size
    allocation slots.

    Larger allocations are serviced by the heap's variable-size
    segment allocator, which maintains and coalesces free blocks.
    
    Very large allocations may bypass the normal heap backend and
    receive a dedicated VirtualAlloc region, backed by a segment and block.
    
    This is conceptually similar to the kernel pool allocator.

                   MT_HEAP
                     |
       +-------------+-------------+
       |                           |
   <= 4096                      > 4096
       |                           |
 size classes              variable blocks
       |                           |
    slabs                    MT_HEAP_SEGMENT
                                   |
                              MT_HEAP_BLOCK
                                   |
                         sufficiently huge
                                   |
                        VirtualAlloc backend

Revision History:

--*/

#include "includes/mtdll.h"

typedef struct _MT_HEAP_BLOCK {
    size_t BlockSize; // Size of the usable memory in this block.
    bool Free; // Whether this block is available for allocation.
    DOUBLY_LINKED_LIST BlockListEntry; // Links this block into its segment.
} MT_HEAP_BLOCK, *PMT_HEAP_BLOCK;

typedef struct _MT_HEAP_SEGMENT {
    void* BaseAddress; // Base address returned by VirtualAlloc.
    size_t SegmentSize; // Total size of this segment.
    DOUBLY_LINKED_LIST SegmentListEntry; // Links this segment into its heap.
    DOUBLY_LINKED_LIST BlockListHead; // Head of the blocks in this segment.
} MT_HEAP_SEGMENT, *PMT_HEAP_SEGMENT;

#define MT_HEAP_BUCKET_COUNT 9
#define MT_HEAP_SLAB_SIZE    (64 * 1024)

typedef struct _MT_HEAP_SLAB {
    void* BaseAddress;
    size_t RegionSize;

    size_t SlotSize;

    // A pointer to the start of the slab slots
    void* SlotsBase;
    uint32_t SlotCount;
    uint32_t FreeCount;

    DOUBLY_LINKED_LIST SlabListEntry;

    //
    // 4096 bits = enough to describe 4096 16-byte slots
    // in a 64 KB slab.
    //
    uint64_t Bitmap[64];

} MT_HEAP_SLAB, * PMT_HEAP_SLAB;


typedef struct _MT_HEAP_BUCKET {
    size_t SlotSize;

    //
    // List of MT_HEAP_SLABs servicing this size class.
    //
    DOUBLY_LINKED_LIST SlabListHead;

} MT_HEAP_BUCKET, * PMT_HEAP_BUCKET;

typedef struct _MT_HEAP {
    HANDLE HeapMutex; // Serializes operations on this heap.
    HEAP_CREATE_OPTIONS Options; // Options controlling this heap's behavior.
    size_t DefaultSegmentSize; // Preferred size when growing this heap.
    size_t MaximumSize; // Maximum combined size of slab and segment regions, or zero for no configured limit.
    size_t CurrentSize; // Combined virtual size currently committed to slab and segment regions.
    MT_HEAP_BUCKET Buckets[MT_HEAP_BUCKET_COUNT]; // Slab buckets, from 16 upto 4096 in powers of 2.
    DOUBLY_LINKED_LIST SegmentListHead; // Head of this heap's segments.
} MT_HEAP, *PMT_HEAP;

#define MT_HEAP_ALIGNMENT 16
#define MT_PAGE_SIZE      4096
#define MT_HEAP_SMALLEST_SLAB 16 // 16 Bytes
#define MT_HEAP_BIGGEST_SLAB 4096 // 4096 Slab

#define ALIGN_UP(Value, Alignment) \
    (((Value) + ((Alignment) - 1)) & ~((Alignment) - 1))

#define MT_HEAP_SEGMENT_HEADER_SIZE \
    ALIGN_UP(sizeof(MT_HEAP_SEGMENT), MT_HEAP_ALIGNMENT)

#define MT_HEAP_BLOCK_HEADER_SIZE \
    ALIGN_UP(sizeof(MT_HEAP_BLOCK), MT_HEAP_ALIGNMENT)

MTDLL_API
MT_HEAP_HANDLE
GetProcessHeap(
    void
)

/*++

    Routine description:

        Returns the process default heap associated with the current PEB.

    Arguments:

        None.

    Return Values:

        The current process heap handle, or NULL when the PEB has no heap.

--*/

{
    return (MT_HEAP_HANDLE) MtCurrentPeb()->ProcessHeap;
}

// Mutex must be held when entering this function
static
PMT_HEAP_SEGMENT
HeapCreateSegment(
    PMT_HEAP Heap,
    size_t MinimumSize
)

{
    const size_t MetadataSize =
        MT_HEAP_SEGMENT_HEADER_SIZE + MT_HEAP_BLOCK_HEADER_SIZE;

    // Reject arithmetic which cannot represent the requested segment.
    if (MinimumSize > SIZE_MAX - MetadataSize) {
        return NULL;
    }

    // The required size is the minimum size, plus the sizeof a segment and a block
    // since these are the initial structs that must exist to describe the memory block(s)
    size_t RequiredSize = MinimumSize + MetadataSize;

    if (RequiredSize > SIZE_MAX - (MT_PAGE_SIZE - 1)) {
        return NULL;
    }

    size_t SegmentSize = ALIGN_UP(RequiredSize, MT_PAGE_SIZE);

    // If this heap growth violates the maximum size of the heap in bytes
    // then fail allocation
    if (SegmentSize > SIZE_MAX - Heap->CurrentSize) {
        return NULL;
    }

    if (Heap->MaximumSize &&
        (Heap->CurrentSize > Heap->MaximumSize ||
         SegmentSize > Heap->MaximumSize - Heap->CurrentSize)) {
        return NULL;
    }

    // Set the protection for the segment
    const USER_PROTECTION_TYPE Protection = (Heap->Options & HEAP_CREATE_ENABLE_EXECUTE) ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
    PMT_HEAP_SEGMENT Segment = (PMT_HEAP_SEGMENT)VirtualAlloc(NULL, SegmentSize, Protection);

    if (!Segment) return NULL;

    // Segment header lives at the beginning of the allocation, and every allocation has its own block.
    // Set members.
    Segment->BaseAddress = Segment;
    Segment->SegmentSize = SegmentSize;
    InitializeListHead(&Segment->BlockListHead);

    // Immediately after the initial segment is the initial free block, which covers the entire size.
    PMT_HEAP_BLOCK HeapBlock = (PMT_HEAP_BLOCK)((uint8_t*)Segment + MT_HEAP_SEGMENT_HEADER_SIZE);
    HeapBlock->BlockSize = SegmentSize - MT_HEAP_SEGMENT_HEADER_SIZE - MT_HEAP_BLOCK_HEADER_SIZE;

    // The initial block is free.
    HeapBlock->Free = true;

    // Insert the block into the segment head list
    InsertTailList(&Segment->BlockListHead, &HeapBlock->BlockListEntry);

    // Add this segment to the heap
    InsertTailList(&Heap->SegmentListHead, &Segment->SegmentListEntry);

    // Add new size to current size
    Heap->CurrentSize += SegmentSize;

    return Segment;
}

static
PMT_HEAP_SLAB
HeapCreateSlab(
    IN PMT_HEAP Heap,
    IN PMT_HEAP_BUCKET Bucket
)

{
    // Slab allocations are part of the heap committed size, they cannot go above it.
    if (MT_HEAP_SLAB_SIZE > SIZE_MAX - Heap->CurrentSize) {
        return NULL;
    }

    if (Heap->MaximumSize) {
        if (Heap->CurrentSize > Heap->MaximumSize || MT_HEAP_SLAB_SIZE > Heap->MaximumSize - Heap->CurrentSize) {
            return NULL;
        }
    }

    const USER_PROTECTION_TYPE Protection =
        (Heap->Options & HEAP_CREATE_ENABLE_EXECUTE)
        ? PAGE_EXECUTE_READWRITE
        : PAGE_READWRITE;

    // Allocate the slab now
    PMT_HEAP_SLAB Slab = (PMT_HEAP_SLAB)VirtualAlloc(NULL, MT_HEAP_SLAB_SIZE, Protection);

    if (!Slab) return NULL;

    // Set slab defaults
    Slab->BaseAddress = Slab;
    Slab->RegionSize = MT_HEAP_SLAB_SIZE;
    Slab->SlotSize = Bucket->SlotSize;

    // Slots begin after this slab metadata, they are aligned to 16 bytes.
    Slab->SlotsBase = (void*)ALIGN_UP((uintptr_t)Slab + sizeof(MT_HEAP_SLAB), MT_HEAP_ALIGNMENT);

    // Calculate metadata size, by subtracting the base (byte aligned slots address) with the Slab address
    size_t MetadataSize = (size_t)((uint8_t*)Slab->SlotsBase - (uint8_t*)Slab);

    // The slot count is each slab size (without the header metadata) divided by the slot size
    Slab->SlotCount = (uint32_t)((MT_HEAP_SLAB_SIZE - MetadataSize) / Slab->SlotSize);

    // The slab free count is the entire slots right now.
    Slab->FreeCount = Slab->SlotCount;

    // VirtualAlloc already zeroes the memory given
    // meaning, the Bitmap array starts empty (0 = free), no need to zero it out
    Heap->CurrentSize += MT_HEAP_SLAB_SIZE;

    // Insert the slab into the bucket slab list.
    InsertTailList(
        &Bucket->SlabListHead,
        &Slab->SlabListEntry
    );

    return Slab;
}

static
void*
HeapClaimSlabSlot(
    IN PMT_HEAP_SLAB Slab
)

{
    // If the Slab has no slots left, then the caller should allocate more
    if (Slab->FreeCount == 0) {
        return NULL;
    }

    // Search the correct slot
    for (uint32_t i = 0; i < Slab->SlotCount; i++) {
        uint32_t WordIndex = i / 64;
        uint32_t BitIndex = i % 64;
        uint64_t Mask = 1ULL << BitIndex;

        if ((Slab->Bitmap[WordIndex] & Mask) == 0) {
            // 0 means free, lets set it to 1
            // meaning lets fucking claim this slot
            // Remember we are under mutex, no need for interlocked no nothing, it will be visible
            Slab->Bitmap[WordIndex] |= Mask;
            Slab->FreeCount--;

            // Return the allocated (found) slab slot
            return ((uint8_t*)Slab->SlotsBase + ((size_t)i * Slab->SlotSize));
        }
    }

    // We should NOT reach here
    // this explictly means list corruption
    // a user probably used HEAP_NO_SERIALIZE when SERIALIZATION is required (2 or more threads touching the heap the same time)
    // w programmer tho
    // assert(false);
    return NULL;
}

static
inline
size_t
HeapGetBucketIndex(
    size_t Size
)
{
    size_t BucketSize = MT_HEAP_SMALLEST_SLAB;

    for (size_t i = 0; i < MT_HEAP_BUCKET_COUNT; i++) {
        if (Size <= BucketSize) {
            return i;
        }

        BucketSize <<= 1;
    }

    return SIZE_MAX;
}

MTDLL_API
MT_HEAP_HANDLE
HeapCreate(
    IN HEAP_CREATE_OPTIONS Options,
    IN size_t InitialSize,
    IN size_t MaximumSize
)

/*++

    Routine description:

        Creates a private user-mode heap with the requested growth policy.

    Arguments:

        [IN] Options - Flags controlling serialization, exceptions, and
        executable heap memory.
        [IN] InitialSize - The preferred initial segment size in bytes.
        [IN] MaximumSize - The maximum combined heap region size, or zero for
        no configured maximum.

    Return Values:

        A heap handle on success, or NULL when the options, sizes, or required
        allocations are invalid.

    Notes:

        The returned heap must be destroyed with HeapDestroy when it is no
        longer needed. The process default heap cannot be destroyed.

--*/

{
    // If no initial size, its just 1 page.
    if (InitialSize == 0) InitialSize = 4096;

    if (MaximumSize && InitialSize > MaximumSize) {
        // The initial size must not be above the maximum size, if a maximum size is present.
        return NULL;
    }

    const uint32_t ValidOptions =
        HEAP_NO_SERIALIZE |
        HEAP_GENERATE_EXCEPTIONS |
        HEAP_CREATE_ENABLE_EXECUTE;

    if (((uint32_t)Options & ~ValidOptions) != 0) {
        // Reject unknown option bits while allowing supported options to be combined.
        return NULL;
    }

    // Create the initial MT_HEAP
    PMT_HEAP Heap = (PMT_HEAP)VirtualAlloc(NULL, sizeof(MT_HEAP), PAGE_READWRITE);
    if (!Heap) {
        return NULL;
    }

    // Set initial heap members
    Heap->Options = Options;
    Heap->MaximumSize = MaximumSize;
    Heap->CurrentSize = 0;
    Heap->DefaultSegmentSize = InitialSize;
    Heap->HeapMutex = MT_INVALID_HANDLE;
    InitializeListHead(&Heap->SegmentListHead);

    if ((Options & HEAP_NO_SERIALIZE) == 0) {
        // Create mutex if the heap should be serialized
        Heap->HeapMutex = CreateMutex(false, NULL);
        if (Heap->HeapMutex == MT_INVALID_HANDLE) {
            VirtualFree(Heap, 0, MEM_RELEASE);
            return NULL;
        }
    }
    else {
        // This is so the code down below at the failure point can pass.
        Heap->HeapMutex = MT_INVALID_HANDLE;
    }

    // Allocate the initial heap segment.
    PMT_HEAP_SEGMENT Segment = HeapCreateSegment(Heap, InitialSize);

    if (!Segment) {
        // The initial heap segment could not be created, destroy mutex if created
        // and free the initial heap alloc.
        if (Heap->HeapMutex != MT_INVALID_HANDLE) {
            CloseHandle(Heap->HeapMutex);
        }

        VirtualFree(Heap, 0, MEM_RELEASE);
        return NULL;
    }

    // Initialize the heap slab buckets for this heap
    size_t SlotSize = MT_HEAP_SMALLEST_SLAB;

    for (size_t i = 0; i < MT_HEAP_BUCKET_COUNT; i++) {
        Heap->Buckets[i].SlotSize = SlotSize;
        InitializeListHead(&Heap->Buckets[i].SlabListHead);

        // ** 2
        SlotSize <<= 1;
    }

    // Success.
    return (PMT_HEAP)Heap;
}

static
inline
PMT_HEAP_SLAB
HeapFindAvailableSlab(
    IN PMT_HEAP_BUCKET SlabBucket
)

{
    // Loop the doubly linked list and search for a free slab in this bucket.
    PDOUBLY_LINKED_LIST Head = &SlabBucket->SlabListHead;
    PDOUBLY_LINKED_LIST Current = SlabBucket->SlabListHead.Flink;
    PMT_HEAP_SLAB FoundSlab = NULL;

    while (Head != Current) {
        
        // Get the slab in the list
        PMT_HEAP_SLAB Slab = CONTAINING_RECORD(Current, MT_HEAP_SLAB, SlabListEntry);

        if (Slab->FreeCount > 0) {
            // Found a free slab.
            FoundSlab = Slab;
            break;
        }

        // Advance to the next one, this one isn't empty.
        Current = Current->Flink;
    }

    return FoundSlab;
}

static
PMT_HEAP_BLOCK
HeapClaimFreeBlock(
    IN PMT_HEAP Heap,
    IN size_t RequestedSize
)

{
    // Align requested size up.
    // And make sure it does not overflow.
    if (RequestedSize > SIZE_MAX - (MT_HEAP_ALIGNMENT - 1)) {
        return NULL;
    }

    RequestedSize = ALIGN_UP(RequestedSize, MT_HEAP_ALIGNMENT);

    // Walk the segment list in the heap.
    PDOUBLY_LINKED_LIST Head = &Heap->SegmentListHead;
    PDOUBLY_LINKED_LIST Current = Head->Flink;

    while (Head != Current) {
        PMT_HEAP_SEGMENT Segment = CONTAINING_RECORD(Current, MT_HEAP_SEGMENT, SegmentListEntry);

        // Walk this segment block list head
        PDOUBLY_LINKED_LIST HeadBlock = &Segment->BlockListHead;
        PDOUBLY_LINKED_LIST CurrentBlock = HeadBlock->Flink;

        while (HeadBlock != CurrentBlock) {
            PMT_HEAP_BLOCK Block = CONTAINING_RECORD(CurrentBlock, MT_HEAP_BLOCK, BlockListEntry);

            // If the block size matches the minimum size, then we can use this one.
            if (Block->Free && Block->BlockSize >= RequestedSize) {
                Block->Free = false;

                // If the block size matches exactly the allocation, then splitting is not needed.
                if (Block->BlockSize == RequestedSize) {
                    // Just return the block directly
                    return Block;
                }
                else {
                    // The block must be splitted, but its splitting size (The remainder) must be 16 bytes atleast
                    if (Block->BlockSize - RequestedSize >= MT_HEAP_BLOCK_HEADER_SIZE + MT_HEAP_ALIGNMENT) {
                        // The block can be split
                        // First create the new block header
                        PMT_HEAP_BLOCK NewBlock = (PMT_HEAP_BLOCK)((uint8_t*)Block + MT_HEAP_BLOCK_HEADER_SIZE + RequestedSize);

                        // Set its sizes and link it to the list.
                        NewBlock->Free = true;
                        NewBlock->BlockSize = Block->BlockSize - MT_HEAP_BLOCK_HEADER_SIZE - RequestedSize;

                        // Insert into the list, we insert right after the previous block (old block, the one we split from)
                        InsertHeadList(
                            &Block->BlockListEntry,
                            &NewBlock->BlockListEntry
                        );

                        // The previous block size must be changed now
                        Block->BlockSize = RequestedSize;

                        // Return the old block
                        return Block;
                    }
                    else {
                        // Return the block, it cannot be split
                        return Block;
                    }
                }
            }

            CurrentBlock = CurrentBlock->Flink;
        }

        Current = Current->Flink;
    }

    return NULL;
}

static
void*
MtpHeapAllocationFailure(
    IN bool GenerateExceptions,
    IN MTSTATUS FailureStatus
)

{
    // If user wants exceptions, raise instead of returning NULL.
    if (GenerateExceptions) {
        RaiseException(
            (uint32_t)FailureStatus,
            0,
            0,
            NULL
        );
    }

    return NULL;
}

// Heap mutex must be held when entering this function.
static
void*
MtpAllocateHeapBlockLocked(
    IN PMT_HEAP Heap,
    IN HEAP_ALLOCATE_OPTIONS Options,
    IN size_t AllocationSize,
    OUT MTSTATUS* FailureStatus
)

{
    void* ReturnedAllocation = NULL;
    *FailureStatus = MT_NO_MEMORY;

    if (AllocationSize <= MT_HEAP_BIGGEST_SLAB) {
        size_t BucketIndex = HeapGetBucketIndex(AllocationSize);
        PMT_HEAP_BUCKET Bucket = &Heap->Buckets[BucketIndex];
        PMT_HEAP_SLAB Slab = HeapFindAvailableSlab(Bucket);

        if (!Slab) {
            Slab = HeapCreateSlab(Heap, Bucket);
            if (!Slab) return NULL;
        }

        ReturnedAllocation = HeapClaimSlabSlot(Slab);
        if (!ReturnedAllocation) {
            *FailureStatus = MT_ACCESS_VIOLATION;
            return NULL;
        }

        if (Options & HEAP_ALLOCATE_ZERO_MEMORY) {
            memset(ReturnedAllocation, 0, Slab->SlotSize);
        }

        return ReturnedAllocation;
    }

    PMT_HEAP_BLOCK Block = HeapClaimFreeBlock(Heap, AllocationSize);

    if (!Block) {
        size_t GrowthSize = AllocationSize > Heap->DefaultSegmentSize ?
            AllocationSize : Heap->DefaultSegmentSize;

        PMT_HEAP_SEGMENT Segment = HeapCreateSegment(Heap, GrowthSize);
        if (!Segment) return NULL;

        Block = HeapClaimFreeBlock(Heap, AllocationSize);
        if (!Block) {
            *FailureStatus = MT_ACCESS_VIOLATION;
            return NULL;
        }
    }

    ReturnedAllocation =
        (void*)((uint8_t*)Block + MT_HEAP_BLOCK_HEADER_SIZE);

    if (Options & HEAP_ALLOCATE_ZERO_MEMORY) {
        memset(ReturnedAllocation, 0, AllocationSize);
    }

    return ReturnedAllocation;
}

// Address returned is 16 byte aligned
MTDLL_API
void*
HeapAlloc(
    IN MT_HEAP_HANDLE Heap,
    IN HEAP_ALLOCATE_OPTIONS Options,
    IN size_t AllocationSize
)

/*++

    Routine description:

        Allocates an aligned block from a heap's slab or segment backend.

    Arguments:

        [IN] Heap - The heap from which memory is allocated.
        [IN] Options - Flags controlling serialization, zeroing, and allocation
        failure behavior.
        [IN] AllocationSize - The requested number of usable bytes.

    Return Values:

        A 16-byte-aligned allocation on success, or NULL when allocation
        fails. A requested exception is raised instead of returning NULL.

    Notes:

        Requests up to 4096 bytes use size-class slabs; larger requests use
        variable-size heap segments.

--*/

{
    bool GenerateExceptions =
        (Options & HEAP_ALLOCATE_GENERATE_EXCEPTIONS) != 0;

    // If a nullptr is given return NULL
    if (!Heap) {
        return MtpHeapAllocationFailure(
            GenerateExceptions,
            MT_ACCESS_VIOLATION
        );
    }

    GenerateExceptions = GenerateExceptions ||
        (Heap->Options & HEAP_GENERATE_EXCEPTIONS) != 0;

    // If invalid options are given, return NULL
    const uint32_t ValidOptions = HEAP_ALLOCATE_GENERATE_EXCEPTIONS | HEAP_ALLOCATE_NO_SERIALIZE | HEAP_ALLOCATE_ZERO_MEMORY;

    if ((uint32_t)(Options & ~ValidOptions) != 0) {
        return MtpHeapAllocationFailure(
            GenerateExceptions,
            MT_ACCESS_VIOLATION
        );
    }

    // If the allocation size is below the smallest slab size, the allocation size will be the smallest slab size
    if (AllocationSize < MT_HEAP_SMALLEST_SLAB) AllocationSize = MT_HEAP_SMALLEST_SLAB;

    // If HEAP_NO_SERIALIZE is passed, then we will not acquire the mutex
    // But if it is not passed, we will act based on the Heap options.
    bool SerializeHeap = !(Options & HEAP_ALLOCATE_NO_SERIALIZE) && !(Heap->Options & HEAP_NO_SERIALIZE);

    if (SerializeHeap) {
        uint32_t ReturnedCode = WaitForSingleObject(Heap->HeapMutex, MT_INFINITE);

        if (ReturnedCode == WAIT_ABANDONED_0) {
            // The mutex belongs to this thread now, but the previous owner may
            // have left the heap metadata inconsistent.
            ReleaseMutex(Heap->HeapMutex);
            return MtpHeapAllocationFailure(
                GenerateExceptions,
                MT_ACCESS_VIOLATION
            );
        }

        if (ReturnedCode != WAIT_OBJECT_0) {
            // The mutex was not acquired, so it must not be released here.
            return MtpHeapAllocationFailure(
                GenerateExceptions,
                MT_ACCESS_VIOLATION
            );
        }
    }

    MTSTATUS FailureStatus;
    void* ReturnedAllocation = MtpAllocateHeapBlockLocked(
        Heap,
        Options,
        AllocationSize,
        &FailureStatus
    );

    // If serialization, then release the mutex
    if (SerializeHeap) {
        ReleaseMutex(Heap->HeapMutex);
    }

    if (!ReturnedAllocation) {
        return MtpHeapAllocationFailure(
            GenerateExceptions,
            FailureStatus
        );
    }

    return ReturnedAllocation;
}

static
bool
HeapFreeSlabAllocation(
    IN PMT_HEAP Heap,
    IN void* Address
)

{
    if (!Address || !Heap) return false;

    // Search every size bucket until we find this pointer (or a size that contains it)
    for (size_t BucketIndex = 0; BucketIndex < MT_HEAP_BUCKET_COUNT; BucketIndex++) {
        PMT_HEAP_BUCKET Bucket = &Heap->Buckets[BucketIndex];

        // Iterate over all the slabs in this bucket
        PDOUBLY_LINKED_LIST Head = &Bucket->SlabListHead;
        PDOUBLY_LINKED_LIST Current = Head->Flink;

        while (Head != Current) {

            // Get the slab for this bucket index
            PMT_HEAP_SLAB Slab = CONTAINING_RECORD(Current, MT_HEAP_SLAB, SlabListEntry);

            // Get slot start base
            uintptr_t SlotsStart = (uintptr_t)Slab->SlotsBase;

            // Calculate the end of the slots so we can know when to stop iterating
            uintptr_t SlotsEnd = (uintptr_t)SlotsStart + ((size_t)Slab->SlotCount * Slab->SlotSize);

            // Does this slab owns the address given to free
            if ((uintptr_t)Address >= SlotsStart && (uintptr_t)Address < SlotsEnd) {
                // It does, we will now calculate if Address is actually the address given by HeapAlloc
                size_t Offset = (uintptr_t)Address - SlotsStart;

                if ((Offset % Slab->SlotSize) != 0) {
                    return false;
                }

                // Get the slot index, word index, and mask for each slab slot bit.
                uint32_t SlotIndex = (uint32_t)(Offset / Slab->SlotSize);
                uint32_t WordIndex = SlotIndex / 64;
                uint32_t BitIndex = SlotIndex % 64;
                uint64_t Mask = 1ULL << BitIndex;

                // Bit being 0 means the slot was not allocated
                // this means a double free (or just an invalid ptr)
                if ((Slab->Bitmap[WordIndex] & Mask) == 0) {
                    return false;
                }

                // Clear the allocated bit (signal its free), increment the count and return.
                Slab->Bitmap[WordIndex] &= ~Mask;
                Slab->FreeCount++;
                return true;
            }

            Current = Current->Flink;
        }
    }

    // Address isnt in any slab.
    return false;
}

static
bool
HeapFreeSegmentAllocation(
    IN PMT_HEAP Heap,
    IN void* Address
)

{
    // Walk every segment, every block in every segment, and find the block associated with the address
    PDOUBLY_LINKED_LIST Head = &Heap->SegmentListHead;
    PDOUBLY_LINKED_LIST Current = Head->Flink;

    while (Head != Current) {
        PMT_HEAP_SEGMENT Segment = CONTAINING_RECORD(Current, MT_HEAP_SEGMENT, SegmentListEntry);

        // Walk every block in this segment
        PDOUBLY_LINKED_LIST HeadBlock = &Segment->BlockListHead;
        PDOUBLY_LINKED_LIST CurrentBlock = HeadBlock->Flink;

        while (HeadBlock != CurrentBlock) {

            PMT_HEAP_BLOCK Block = CONTAINING_RECORD(CurrentBlock, MT_HEAP_BLOCK, BlockListEntry);

            // Calculate if this address is stemed from this block address.
            void* BlockAddress =
                (uint8_t*)Block + MT_HEAP_BLOCK_HEADER_SIZE;

            if (Address == BlockAddress) {
                // This is the found allocation!
                // Check for double free, and if its good (no double), free it.
                if (Block->Free) {
                    // The block is already free..
                    return false;
                }

                // Set the block as free now
                Block->Free = true;

                // Now coalesce adjacent free blocks.
                // First check if the backward is free and is a valid block
                if (Block->BlockListEntry.Blink != HeadBlock) {
                    // The previous block isnt BlockListHead, meaning its a valid block
                    // try to see if we can coalesce it
                    PMT_HEAP_BLOCK Previous = CONTAINING_RECORD(Block->BlockListEntry.Blink, MT_HEAP_BLOCK, BlockListEntry);

                    // If the previous is free too, then we can coalesce this.
                    if (Previous->Free) {
                        Previous->BlockSize += MT_HEAP_BLOCK_HEADER_SIZE + Block->BlockSize;

                        // Remove the current block from the block list in this segment, and revert the loop to the previous block
                        // which would now be the coalesced block
                        RemoveEntryList(&Block->BlockListEntry);
                        Block = Previous;
                    }
                }

                // Coalesce the forward link now
                // If previous coalesce succeeded, then this block is now the coalesced previous block and the current (now overwritten) block.
                if (Block->BlockListEntry.Flink != HeadBlock) {
                    PMT_HEAP_BLOCK Next = CONTAINING_RECORD(Block->BlockListEntry.Flink, MT_HEAP_BLOCK, BlockListEntry);

                    if (Next->Free) {
                        // Since we operate on the Next now, we basically operate on this block size and list entry, while removing the next one.
                        Block->BlockSize += MT_HEAP_BLOCK_HEADER_SIZE + Next->BlockSize;

                        // Remove the next block from the block list in this segment
                        RemoveEntryList(&Next->BlockListEntry);

                        // No need to revert to previous, we ARE the previous if we were "Next"
                        // confusing stuff isnt it?
                    }
                }

                // Free success.
                return true;
            }

            CurrentBlock = CurrentBlock->Flink;
        }

        Current = Current->Flink;
    }

    // No block associated with this address found.
    return false;
}

MTDLL_API
bool
HeapFree(
    IN MT_HEAP_HANDLE Heap,
    IN HEAP_FREE_OPTIONS Options,
    IN void* AllocatedMemory
)

/*++

    Routine description:

        Returns an allocation to its heap and coalesces adjacent free segment
        blocks when applicable.

    Arguments:

        [IN] Heap - The heap that owns the allocation.
        [IN] Options - Flags controlling heap serialization.
        [IN] AllocatedMemory - The allocation returned by HeapAlloc.

    Return Values:

        true when the allocation is freed, or false for an invalid, already
        freed, or foreign pointer.

--*/
{
    // Invalid ptrs
    if (!Heap || !AllocatedMemory) {
        return false;
    }

    const uint32_t ValidOptions =
        HEAP_FREE_NO_SERIALIZE;

    if ((uint32_t)(Options & ~ValidOptions) != 0) {
        return false;
    }

    // Check if the heap needs to be serialized. (mutex hold)
    bool SerializeHeap = !(Options & HEAP_FREE_NO_SERIALIZE) && !(Heap->Options & HEAP_NO_SERIALIZE);

    if (SerializeHeap) {
        uint32_t ReturnedCode =
            WaitForSingleObject(
                Heap->HeapMutex,
                MT_INFINITE
            );

        if (ReturnedCode == WAIT_ABANDONED_0) {
            // The mutex belongs to this thread now, but the previous owner may
            // have left the heap metadata inconsistent.
            ReleaseMutex(Heap->HeapMutex);
            return false;
        }

        if (ReturnedCode != WAIT_OBJECT_0) {
            // The mutex was not acquired, so it must not be released here.
            return false;
        }
    }

    // Call internal function
    // We do NOT know if the allocation given is a slab or a segment
    // so we run both functions, if we wanted to resolve this we could return to the caller a struct with the size and allocated block
    // or the caller could have given us the size, but this is really prone to errors, bcz programmers are humans.
    bool Result =
        HeapFreeSlabAllocation(
            Heap,
            AllocatedMemory
        );

    if (!Result) {
        Result = HeapFreeSegmentAllocation(Heap, AllocatedMemory);
    }

    // Release mutex if serialization
    if (SerializeHeap) {
        ReleaseMutex(Heap->HeapMutex);
    }

    // Return if given pointer to allocated memory has freed the allocated memory in the allocated memories inside a 64bit system
    // what
    return Result;
}

MTDLL_API
bool
HeapDestroy(
    IN MT_HEAP_HANDLE HeapHandle
)

/*++

    Routine description:

        Destroys a private heap and releases its slabs, segments, mutex, and
        control block.

    Arguments:

        [IN] HeapHandle - The private heap to destroy.

    Return Values:

        true when all heap resources are released, or false when the handle is
        invalid, identifies the process heap, or cleanup fails.

    Notes:

        The caller must stop using the heap before destruction begins.

--*/

{
    if (!HeapHandle || HeapHandle == GetProcessHeap()) {
        // Reject process default heap, or a null ptr given.
        return false;
    }

    // Acquire the heap mutex before destroying it
    // depending on the SERIALIZE option
    bool SerializeHeap = !(HeapHandle->Options & HEAP_NO_SERIALIZE);

    if (SerializeHeap) {
        uint32_t ReturnedCode =
            WaitForSingleObject(
                HeapHandle->HeapMutex,
                MT_INFINITE
            );

        if (ReturnedCode == WAIT_ABANDONED_0) {
            // Mutex acquired because a thread was terminated while holding it
            // do not continue.
            ReleaseMutex(HeapHandle->HeapMutex);
            return false;
        }

        if (ReturnedCode != WAIT_OBJECT_0) {
            // The mutex was not acquired, so it must not be released here.
            return false;
        }
    }

    bool HeapDestroyed = true;

    // Save the mutex handle, since we free MT_HEAP too
    HANDLE MutexHandle = HeapHandle->HeapMutex;

    // Free every slab in the heap
    for (size_t i = 0; i < MT_HEAP_BUCKET_COUNT; i++) {
        PMT_HEAP_BUCKET Bucket = &HeapHandle->Buckets[i];

        // Free every allocated slab in this bucket.
        PDOUBLY_LINKED_LIST Head = &Bucket->SlabListHead;
        PDOUBLY_LINKED_LIST Current = Head->Flink;

        while (Head != Current) {

            PMT_HEAP_SLAB Slab = CONTAINING_RECORD(Current, MT_HEAP_SLAB, SlabListEntry);

            // Save the next ptr since we literally destroy Current using VirtualFree.
            PDOUBLY_LINKED_LIST Next = Current->Flink;

            if (!VirtualFree(Slab, 0, MEM_RELEASE)) {
                HeapDestroyed = false;
            }

            Current = Next;
        }
    }

    // Free every segment in the heap
    PDOUBLY_LINKED_LIST Head = &HeapHandle->SegmentListHead;
    PDOUBLY_LINKED_LIST Current = Head->Flink;

    while (Head != Current) {
        PMT_HEAP_SEGMENT Segment = CONTAINING_RECORD(Current, MT_HEAP_SEGMENT, SegmentListEntry);

        // Save the next ptr since we literally destroy Current using VirtualFree.
        PDOUBLY_LINKED_LIST Next = Current->Flink;

        if (!VirtualFree(Segment, 0, MEM_RELEASE)) {
            HeapDestroyed = false;
        }

        Current = Next;
    }

    if (SerializeHeap) {
        if (!ReleaseMutex(MutexHandle)) {
            HeapDestroyed = false;
        }

        if (!CloseHandle(MutexHandle)) {
            HeapDestroyed = false;
        }
    }

    // Free the heap control block last.
    if (!VirtualFree(HeapHandle, 0, MEM_RELEASE)) {
        HeapDestroyed = false;
    }

    return HeapDestroyed;
}

MTDLL_API
size_t
HeapSize(
    IN MT_HEAP_HANDLE HeapHandle,
    IN HEAP_SIZE_OPTIONS Options,
    IN void* AllocatedMemory
)

/*++

    Routine description:

        Validates an allocation and returns its usable size.

    Arguments:

        [IN] HeapHandle - The heap that owns the allocation.
        [IN] Options - Flags controlling heap serialization.
        [IN] AllocatedMemory - The allocation to inspect.

    Return Values:

        The usable allocation size, or MT_HEAP_SIZE_ERROR when the heap,
        options, pointer, or allocation state is invalid.

--*/

{
    if (!HeapHandle || !AllocatedMemory) return MT_HEAP_SIZE_ERROR;

    const uint32_t ValidOptions =
        HEAP_SIZE_NO_SERIALIZE;

    if ((uint32_t)(Options & ~ValidOptions) != 0) {
        return MT_HEAP_SIZE_ERROR;
    }

    bool SerializeHeap = !(Options & HEAP_SIZE_NO_SERIALIZE) && !(HeapHandle->Options & HEAP_NO_SERIALIZE);

    if (SerializeHeap) {
        uint32_t ReturnedCode = WaitForSingleObject(HeapHandle->HeapMutex, MT_INFINITE);

        if (ReturnedCode == WAIT_ABANDONED_0) {
            // The mutex belongs to this thread now, but the previous owner may
            // have left the heap metadata inconsistent.
            ReleaseMutex(HeapHandle->HeapMutex);
            return MT_HEAP_SIZE_ERROR;
        }

        if (ReturnedCode != WAIT_OBJECT_0) {
            // The mutex was not acquired, so it must not be released here.
            return MT_HEAP_SIZE_ERROR;
        }
    }

    size_t FoundSize = MT_HEAP_SIZE_ERROR;

    // Search through the slabs first.
    for (size_t i = 0; i < MT_HEAP_BUCKET_COUNT; i++) {
        PMT_HEAP_BUCKET Bucket = &HeapHandle->Buckets[i];
        PDOUBLY_LINKED_LIST Head = &Bucket->SlabListHead;
        PDOUBLY_LINKED_LIST Current = Head->Flink;

        while (Head != Current) {

            PMT_HEAP_SLAB Slab = CONTAINING_RECORD(Current, MT_HEAP_SLAB, SlabListEntry);

            // See if the slab matches this pointer.
            // Get slot start base
            uintptr_t SlotsStart = (uintptr_t)Slab->SlotsBase;

            // Calculate the end of the slots.
            uintptr_t SlotsEnd = (uintptr_t)SlotsStart + ((size_t)Slab->SlotCount * Slab->SlotSize);

            // Does this slab owns the address given
            if ((uintptr_t)AllocatedMemory >= SlotsStart && (uintptr_t)AllocatedMemory < SlotsEnd) {
                size_t Offset = (uintptr_t)AllocatedMemory - SlotsStart;

                // The allocated memory ptr must be the start of the allocated slab
                if ((Offset % Slab->SlotSize) != 0) {
                    goto Cleanup;
                }

                // Get the slot index, word index, and mask for each slab slot bit.
                uint32_t SlotIndex = (uint32_t)(Offset / Slab->SlotSize);
                uint32_t WordIndex = SlotIndex / 64;
                uint32_t BitIndex = SlotIndex % 64;
                uint64_t Mask = 1ULL << BitIndex;

                // Bit being 0 means the slot was not allocated
                if ((Slab->Bitmap[WordIndex] & Mask) == 0) {
                    goto Cleanup;
                }

                // It does contain the address, it is validated too, return slab size.
                FoundSize = Slab->SlotSize;
                goto Cleanup;
            }

            Current = Current->Flink;
        }
    }

    // Search through the segments now, slabs didnt return anything
    PDOUBLY_LINKED_LIST Head = &HeapHandle->SegmentListHead;
    PDOUBLY_LINKED_LIST Current = Head->Flink;

    while (Head != Current) {
        PMT_HEAP_SEGMENT Segment = CONTAINING_RECORD(Current, MT_HEAP_SEGMENT, SegmentListEntry);
        PDOUBLY_LINKED_LIST HeadBlock = &Segment->BlockListHead;
        PDOUBLY_LINKED_LIST CurrentBlock = HeadBlock->Flink;

        while (HeadBlock != CurrentBlock) {

            PMT_HEAP_BLOCK Block = CONTAINING_RECORD(CurrentBlock, MT_HEAP_BLOCK, BlockListEntry);

            void* BlockUserAddr = ((uint8_t*)Block + MT_HEAP_BLOCK_HEADER_SIZE);

            if (BlockUserAddr == AllocatedMemory) {

                if (Block->Free) {
                    // The block is free, the user has requested size for a free ptr.
                    // That is an error.
                    goto Cleanup;
                }

                // Found the allocated ptr, return its size.
                FoundSize = Block->BlockSize;
                goto Cleanup;
            }

            CurrentBlock = CurrentBlock->Flink;
        }

        Current = Current->Flink;
    }

Cleanup:
    if (SerializeHeap) {
        ReleaseMutex(HeapHandle->HeapMutex);
    }

    return FoundSize;
}

MTDLL_API
void*
HeapReAlloc(
    IN MT_HEAP_HANDLE HeapHandle,
    IN HEAP_REALLOCATION_OPTIONS Options,
    IN void* AllocatedMemory,
    IN size_t NewReAllocationSize
)

/*++

    Routine description:

        Resizes an existing heap allocation, preserving its previous contents
        up to the old allocation size.

    Arguments:

        [IN] HeapHandle - The heap that owns the allocation.
        [IN] Options - Flags controlling serialization, zeroing, exceptions,
        and in-place-only behavior.
        [IN] AllocatedMemory - The allocation to resize.
        [IN] NewReAllocationSize - The requested new usable size.

    Return Values:

        The resized allocation on success, or NULL when resizing fails or the
        in-place-only request cannot be satisfied. A requested exception is
        raised instead of returning NULL.

    Notes:

        The segment in-place growth optimization is intentionally tracked as a
        later heap feature. The current implementation may allocate a new
        block for growth.

--*/

{
    bool GenerateExceptions =
        (Options & HEAP_REALLOCATE_GENERATE_EXCEPTIONS) != 0;

    if (!HeapHandle) {
        return MtpHeapAllocationFailure(
            GenerateExceptions,
            MT_ACCESS_VIOLATION
        );
    }

    GenerateExceptions = GenerateExceptions ||
        (HeapHandle->Options & HEAP_GENERATE_EXCEPTIONS) != 0;

    if (!AllocatedMemory || !NewReAllocationSize) {
        return MtpHeapAllocationFailure(
            GenerateExceptions,
            MT_ACCESS_VIOLATION
        );
    }

    const uint32_t ValidOptions =
        HEAP_REALLOCATE_GENERATE_EXCEPTIONS |
        HEAP_REALLOCATE_IN_PLACE_ONLY |
        HEAP_REALLOCATE_NO_SERIALIZE |
        HEAP_REALLOCATE_ZERO_MEMORY;

    if ((uint32_t)(Options & ~ValidOptions) != 0) {
        return MtpHeapAllocationFailure(
            GenerateExceptions,
            MT_ACCESS_VIOLATION
        );
    }

    bool SerializeHeap = !(Options & HEAP_REALLOCATE_NO_SERIALIZE) && !(HeapHandle->Options & HEAP_NO_SERIALIZE);

    if (SerializeHeap) {
        uint32_t ReturnedCode = WaitForSingleObject(HeapHandle->HeapMutex, MT_INFINITE);

        if (ReturnedCode == WAIT_ABANDONED_0) {
            // The mutex belongs to this thread now, but the previous owner may
            // have left the heap metadata inconsistent.
            ReleaseMutex(HeapHandle->HeapMutex);
            return MtpHeapAllocationFailure(
                GenerateExceptions,
                MT_ACCESS_VIOLATION
            );
        }

        if (ReturnedCode != WAIT_OBJECT_0) {
            // The mutex was not acquired, so it must not be released here.
            return MtpHeapAllocationFailure(
                GenerateExceptions,
                MT_ACCESS_VIOLATION
            );
        }
    }

    // Call the heap size function, to compare OldSize vs NewSize
    // HeapReAlloc already owns this heap's mutex when serialization is enabled.
    // Avoid a redundant recursive acquisition and its additional system calls.
    size_t OldSize = HeapSize(HeapHandle, HEAP_SIZE_NO_SERIALIZE, AllocatedMemory);
    void* ReturnedPointer = NULL;
    MTSTATUS FailureStatus = MT_NO_MEMORY;
    if (OldSize == MT_HEAP_SIZE_ERROR) {
        // Size failure, return
        FailureStatus = MT_ACCESS_VIOLATION;
        goto Cleanup;
    }

    // If the old size is BIGGER than the new size, then just return the original pointer
    if (NewReAllocationSize <= OldSize) {
        ReturnedPointer = AllocatedMemory;
        goto Cleanup;
    }
    else if (Options & HEAP_REALLOCATE_IN_PLACE_ONLY) {
        // We cannot relloc in place FOR NOW, until segment coalsce optimization is here
        goto Cleanup;
    }
    else {
        // Allocate a new memory chunk
        // Copy over the old memory contents to new
        // Free the old pointer, and return the new pointer
        HEAP_ALLOCATE_OPTIONS AllocOptions =
            (Options & HEAP_REALLOCATE_ZERO_MEMORY) ?
            HEAP_ALLOCATE_ZERO_MEMORY : HEAP_ALLOCATE_NO_OPTIONS;
        void* NewAllocation = MtpAllocateHeapBlockLocked(
            HeapHandle,
            AllocOptions,
            NewReAllocationSize,
            &FailureStatus
        );

        if (!NewAllocation) {
            goto Cleanup;
        }

        memcpy(NewAllocation, AllocatedMemory, OldSize);

        if (!HeapFree(
            HeapHandle,
            HEAP_FREE_NO_SERIALIZE,
            AllocatedMemory
        )) {
            // If freeing the old pointer fails, free the new one and return NULL.
            HeapFree(
                HeapHandle,
                HEAP_FREE_NO_SERIALIZE,
                NewAllocation
            );
            FailureStatus = MT_ACCESS_VIOLATION;
            goto Cleanup;
        }

        ReturnedPointer = NewAllocation;
        goto Cleanup;
    }


Cleanup:
    if (SerializeHeap) {
        ReleaseMutex(HeapHandle->HeapMutex);
    }

    if (!ReturnedPointer) {
        return MtpHeapAllocationFailure(
            GenerateExceptions,
            FailureStatus
        );
    }

    return ReturnedPointer;
}

MTDLL_API
bool
HeapLock(
    IN MT_HEAP_HANDLE Heap
)

/*++

    Routine description:

        Acquires the serialization mutex for a heap explicitly.

    Arguments:

        [IN] Heap - The heap whose serialization mutex is acquired.

    Return Values:

        true when the mutex is acquired, or false when the heap is invalid,
        nonserialized, abandoned, or cannot be acquired.

--*/

{
    if (!Heap || Heap->Options & HEAP_NO_SERIALIZE) return false;

    // Acquire the mutex
    uint32_t RetVal = WaitForSingleObject(Heap->HeapMutex, MT_INFINITE);

    if (RetVal == WAIT_ABANDONED_0) {
        // The mutex belongs to this thread now, but the previous owner may
        // have left the heap metadata inconsistent.
        ReleaseMutex(Heap->HeapMutex);
        return false;
    }

    if (RetVal != WAIT_OBJECT_0) {
        // The mutex was not acquired, so it must not be released here.
        return false;
    }

    return true;
}

MTDLL_API
bool
HeapUnlock(
    IN MT_HEAP_HANDLE Heap
)

/*++

    Routine description:

        Releases the serialization mutex previously acquired for a heap.

    Arguments:

        [IN] Heap - The heap whose serialization mutex is released.

    Return Values:

        true when the mutex is released, or false when the heap is invalid,
        nonserialized, or the current thread does not own the mutex.

--*/

{
    if (!Heap || Heap->Options & HEAP_NO_SERIALIZE) return false;
    return ReleaseMutex(Heap->HeapMutex);
}
