/*++

Module Name:

    va.c

Purpose:

    This translation unit contains the implementation of virtual address pool of the kernel.

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "../../includes/mm.h"
#include "../../includes/ps.h"

// NONPAGED ----
static uint64_t* g_NonpagedPoolVaBitmap;

// Hint for next search
static volatile uint64_t g_NonpagedPoolHintIndex = 0;

bool
MiInitializePoolVaSpace(
    void
)

/*++

    Routine description:

        Initializes the nonpaged pool virtual address bitmap.

    Arguments:

        None.

    Return Values:

        True or False based on succession.

--*/

{
    // Initialize the nonpaged bitmap first.
    uintptr_t currNpgBitmapVa = MI_NONPAGED_BITMAP_BASE;
    for (size_t i = 0; i < MI_NONPAGED_BITMAP_PAGES_NEEDED; i++) {
        // Request a physical page.
        PAGE_INDEX pfn = MiRequestPhysicalPage(PfnStateZeroed);
        if (pfn == PFN_ERROR) return false; // Would bugcheck, no need for physical page release back. (loop unroll)

        // Get the PTE ptr for the curr va.
        PMMPTE pte = MiGetPtePointer(currNpgBitmapVa);
        if (!pte) return false;
        // Get the physical address of the PFN.
        uint64_t phys = PPFN_TO_PHYSICAL_ADDRESS(INDEX_TO_PPFN(pfn));
        // Map it.
        MI_WRITE_PTE(pte, currNpgBitmapVa, phys, PAGE_PRESENT | PAGE_RW);

        // Set the PFNs states.
        PPFN_ENTRY pfnEntry = INDEX_TO_PPFN(pfn);
        pfnEntry->State = PfnStateActive;
        pfnEntry->Flags = PFN_FLAG_NONPAGED;
        pfnEntry->Descriptor.Mapping.PteAddress = pte;
        pfnEntry->Descriptor.Mapping.Vad = NULL; // Not VAD-backed

        // Advance VA by 4KiB.
        currNpgBitmapVa += VirtualPageSize;
    }

    g_NonpagedPoolVaBitmap = (uint64_t*)MI_NONPAGED_BITMAP_BASE;

    // Both bitmaps are mapped, begin building them.
    // Initialize both bitmaps to FREE.
    size_t nonpaged_bitmap_bytes = (size_t)NONPAGED_POOL_VA_BITMAP_QWORDS * sizeof(uint64_t);

    kmemset(g_NonpagedPoolVaBitmap, 0, nonpaged_bitmap_bytes);
    
    // Initialize hints.
    g_NonpagedPoolHintIndex = 0;

    // Both bitmaps fully setupped
    return true;
}

// Testing and applying functions.

FORCEINLINE
bool
MiBitmapTestBit(
    uint64_t* bitmap,
    size_t bit
)

// Description: Tests a bit in the bitmap provided.
// Return Values: True if bit is set, false otherwise

{
    size_t q = bit >> 6; // QWORD Index
    size_t b = bit & 63; // Bit index within that qword.

    // Atomically read the 64bit word.
    uint64_t value = InterlockedFetchU64((volatile uint64_t*)&bitmap[q]);
    return (value >> b) & 1ULL;
}

FORCEINLINE
bool
MiBitmapTestAndSetBitLocked(
    uint64_t* bitmap,
    size_t bit
)

// Description: This routine tests if the bit isn't set, and if so, sets it, and returns true (all atomically). Otherwise, returns false.

{
    size_t q = bit >> 6;
    size_t b = bit & 63;
    uint64_t mask = (1ULL << b);

    // Atomically OR the mask in and return the original qword value
    uint64_t old_qword = __sync_fetch_and_or(&bitmap[q], mask);

    // Return 'true' if our bit was NOT set in the old value
    return (old_qword & mask) == 0;
}

FORCEINLINE
void
MiBitmapClearBitLocked(
    uint64_t* bitmap,
    size_t bit
)

// Description: Clears a bit from locked in the bitmap.
// Return Values: None.

{
    size_t q = bit >> 6;
    size_t b = bit & 63;
    // ~ signifies the opposite of the set.
    InterlockedAndU64((volatile uint64_t*)&bitmap[q], ~(1ULL << b));
}

FORCEINLINE
uintptr_t
MiIndexToVa(
    uintptr_t poolBase,
    size_t index
)

// Converts a pool base index to its corresponding virtual address.

{
    return poolBase + (index * VirtualPageSize);
}

FORCEINLINE
size_t
MiVaToIndex(
    uintptr_t poolBase,
    uintptr_t va
)

// Converts a VA into its corresponding Pool index.

{
    return (va - poolBase) / VirtualPageSize; // The caller must ensure the VA is in range.
}

uintptr_t
MiAllocatePoolVa(
    IN  POOL_TYPE PoolType,
    IN  size_t NumberOfBytes
)

/*++

    Routine description:

        Searches for a free VA (NumberOfBytes size) in the pool, and returns it.

    Arguments:

        [IN] POOL_TYPE PoolType - The type of pool to return the VA for.
        [IN] size_t NumberOfBytes - The amount of contingious VA bytes to find for.

    Return Values:

        The VA on success, otherwise 0 on failure.

    Notes:

        If **NonPagedPool** ->>> The returned VA is NOT mapped, and is NOT backed by ANY pfn. 

--*/

{
    // Declarations for mixed pools
    size_t total_pages, total_qwords;
    size_t hint;
    uint64_t* bitmap;
    uintptr_t poolBase;
    volatile uint64_t* hintIndexPtr;

    // Calculate pages needed, rounding up.
    size_t NumberOfPages = BYTES_TO_PAGES(NumberOfBytes);
    if (NumberOfPages == 0) return 0;

    // Set-up pool specific parameters.
    if (PoolType == NonPagedPool) {
        total_pages = NONPAGED_POOL_VA_TOTAL_PAGES;
        hint = (size_t)InterlockedFetchU64(&g_NonpagedPoolHintIndex);
        bitmap = g_NonpagedPoolVaBitmap;
        poolBase = MI_NONPAGED_POOL_BASE;
        hintIndexPtr = &g_NonpagedPoolHintIndex;
    }
    else {
        // If we want a NonPagedPool VA space, we use the VAD (and allocate the memory in the process)
        // The caller (probably MmAllocatePoolWithTag(pagedPool)) will set the pool header and return the VA.
        // The base address is NULL, we use VADs to find it.
        void* baseAddr = NULL;
        MTSTATUS status = MmAllocateVirtualMemory(PsGetCurrentProcess(), &baseAddr, NumberOfBytes, VAD_FLAG_WRITE | VAD_FLAG_READ);
        if (MT_FAILURE(status)) {
            return 0;
        }
        return (uintptr_t)baseAddr;
    }

    total_qwords = total_pages / 64;

    // SINGLE PAGE ALLOCATION
    if (NumberOfPages == 1) {
        size_t start_q = (hint / 64) % total_qwords;

        // Scan qword-by-qword
        for (size_t i = 0; i < total_qwords; i++) {
            size_t q_idx = (start_q + i) % total_qwords;

            // rescan loop
            while (true)
            {
                uint64_t qword = bitmap[q_idx];
                if (qword == 0xFFFFFFFFFFFFFFFFULL) {
                    break; // This qword is full, move to the next q_idx
                }

                uint64_t inverted_qword = ~qword;
                unsigned long bit_index_in_qword = __builtin_ctzll(inverted_qword);
                size_t global_bit_idx = (q_idx * 64) + bit_index_in_qword;

                if (MiBitmapTestAndSetBitLocked(bitmap, global_bit_idx)) {
                    // We successfully claimed it!
                    InterlockedExchangeU64(hintIndexPtr, (uint64_t)(global_bit_idx + 1));
                    return MiIndexToVa(poolBase, global_bit_idx);
                }
                // If we failed, another CPU beat us. The while(true) loop
                // will just retry on the same qword.
            }
        }
        return 0; // No free VA pages found
    }

    // CONTINGUOUS PAGE ALLOCATION
    size_t start_idx = hint % total_pages;
    size_t contiguous_found = 0;
    size_t start_of_run_idx = 0;

    // This loop must check every bit.
    for (size_t i = 0; i < total_pages; i++) {
        size_t current_idx = (start_idx + i) % total_pages;

        // We can't use TestAndSet yet. Just read the bit.
        if (MiBitmapTestBit(bitmap, current_idx)) {
            // This bit is set. Reset our contiguous run.
            contiguous_found = 0;
            continue;
        }

        // Free bit.
        if (contiguous_found == 0) {
            // This is the start of a potential run
            start_of_run_idx = current_idx;
        }
        contiguous_found++;

        if (current_idx < start_of_run_idx) {
            contiguous_found = 0; // Wrapped around, reset
            continue;
        }

        // Do we have enough pages?
        if (contiguous_found == NumberOfPages) {
            // We found a potential run from 'start_of_run_idx' for NumberOfPages, attempt to claim all of them.

            size_t j = 0;
            for (; j < NumberOfPages; j++) {
                size_t idx_to_claim = start_of_run_idx + j;

                if (!MiBitmapTestAndSetBitLocked(bitmap, idx_to_claim)) {
                    // WE FAILED! Another CPU grabbed a bit in our run.
                    // We must roll back all the bits we *did* claim.
                    for (size_t k = 0; k < j; k++) {
                        MiBitmapClearBitLocked(bitmap, start_of_run_idx + k);
                    }

                    // Reset contiguous_found and continue the outer search
                    contiguous_found = 0;
                    break; // Break from this 'j' loop
                }
            }

            // If 'j' == NumberOfPages, it means we successfully claimed ALL bits
            if (j == NumberOfPages) {
                InterlockedExchangeU64(hintIndexPtr, (start_of_run_idx + NumberOfPages));
                return MiIndexToVa(poolBase, start_of_run_idx);
            }
            // If we're here, we failed the claim and rolled back, outer loop will continue.
        }
    }

    return 0; // No contiguous range found
}

void
MiFreePoolVaContiguous(
    IN  uintptr_t va,
    IN  size_t NumberOfBytes,
    IN  POOL_TYPE PoolType
)

/*++

    Routine description:

        Frees a VA in the bitmap.

    Arguments:

        [IN] uintptr_t va - The virtual address to free in the bitmap.
        [IN] size_t NumberOfBytes - Number of bytes used to allocate from the VA allocation.
        [IN] POOL_TYPE PoolType - The type of pool to free the VA for.

    Return Values:

        None.

--*/

{
    size_t NumberOfPages = BYTES_TO_PAGES(NumberOfBytes);
    uint64_t* bitmap;
    uintptr_t poolBase;
    uintptr_t poolEnd;

    if (PoolType == NonPagedPool) {
        poolBase = MI_NONPAGED_POOL_BASE;
        poolEnd = MI_NONPAGED_POOL_END;
        bitmap = g_NonpagedPoolVaBitmap;
    }
    else {
        // Paged pool, we deallocate VADs. (NumberOfBytes is ignored)
        MTSTATUS stat = MmFreeVirtualMemory(PsGetCurrentProcess(), (void*)va);

        if (MT_FAILURE(stat)) {
            MeBugCheck(MEMORY_INVALID_FREE);
        }
        
        return;
    }

    if (va < poolBase || va >= poolEnd) return;

    size_t start_idx = MiVaToIndex(poolBase, va);

    // Loop and free all bits in the range
    for (size_t i = 0; i < NumberOfPages; i++) {
        MiBitmapClearBitLocked(bitmap, start_idx + i);
    }
}