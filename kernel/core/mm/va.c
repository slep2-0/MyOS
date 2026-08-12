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

// PAGED ----
static uint64_t* g_PagedPoolVaBitmap;

// Hint for next search
static volatile uint64_t g_PagedPoolHintIndex = 0;

bool
MiInitializePoolVaSpace(
    void
)

/*++

    Routine description:

        Initializes the nonpaged & paged pool virtual address bitmap.

    Arguments:

        None.

    Return Values:

        True or False based on succession.

--*/

{
    // Initialize the nonpaged bitmap first.
    uintptr_t currNpgBitmapVa = MI_NONPAGED_BITMAP_BASE;
    uintptr_t currPgBitmapVa = MI_PAGED_BITMAP_BASE;

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

    for (size_t i = 0; i < MI_PAGED_BITMAP_PAGES_NEEDED; i++) {
        // Request a physical page.
        PAGE_INDEX pfn = MiRequestPhysicalPage(PfnStateZeroed);
        if (pfn == PFN_ERROR) return false; // Would bugcheck, no need for physical page release back. (loop unroll)

        // Get the PTE ptr for the curr va.
        PMMPTE pte = MiGetPtePointer(currPgBitmapVa);
        if (!pte) return false;
        // Get the physical address of the PFN.
        uint64_t phys = PPFN_TO_PHYSICAL_ADDRESS(INDEX_TO_PPFN(pfn));
        // Map it.
        MI_WRITE_PTE(pte, currPgBitmapVa, phys, PAGE_PRESENT | PAGE_RW);

        // Set the PFNs states.
        PPFN_ENTRY pfnEntry = INDEX_TO_PPFN(pfn);
        pfnEntry->State = PfnStateActive;
        pfnEntry->Flags = PFN_FLAG_NONPAGED;
        pfnEntry->Descriptor.Mapping.PteAddress = pte;
        pfnEntry->Descriptor.Mapping.Vad = NULL; // Not VAD-backed

        // Advance VA by 4KiB.
        currPgBitmapVa += VirtualPageSize;
    }

    g_NonpagedPoolVaBitmap = (uint64_t*)MI_NONPAGED_BITMAP_BASE;
    g_PagedPoolVaBitmap = (uint64_t*)MI_PAGED_BITMAP_BASE;

    // Both bitmaps are mapped, begin building them.
    // Initialize both bitmaps to FREE.
    size_t nonpaged_bitmap_bytes = (size_t)NONPAGED_POOL_VA_BITMAP_QWORDS * sizeof(uint64_t);
    size_t paged_bitmap_bytes = (size_t)PAGED_POOL_VA_BITMAP_QWORDS * sizeof(uint64_t);

    kmemset(g_NonpagedPoolVaBitmap, 0, nonpaged_bitmap_bytes);
    kmemset(g_PagedPoolVaBitmap, 0, paged_bitmap_bytes);
    
    // Initialize hints.
    g_NonpagedPoolHintIndex = 0;
    g_PagedPoolHintIndex = 0;

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

/*++

    Routine description:

        Reports whether a virtual-address bitmap bit is set.

    Arguments:

        [IN] bitmap - Allocation bitmap whose bit is examined or changed.
        [IN] bit - Bit index in the allocation bitmap.

    Return Values:

        A nonzero value when the reported condition holds, or zero otherwise.

--*/

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

/*++

    Routine description:

        Tests and sets a virtual-address bitmap bit while the bitmap lock is held.

    Arguments:

        [IN] bitmap - Allocation bitmap whose bit is examined or changed.
        [IN] bit - Bit index in the allocation bitmap.

    Return Values:

        The previous value of the bitmap bit.

--*/

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
bool
MiBitmapClearBitLocked(
    uint64_t* bitmap,
    size_t bit
)

// Description: Clears a bit atomically in the bitmap.
// Return Values: True if the bit was set before it was cleared.

/*++

    Routine description:

        Clears a virtual-address bitmap bit while the bitmap lock is held.

    Arguments:

        [IN] bitmap - Allocation bitmap whose bit is examined or changed.
        [IN] bit - Bit index in the allocation bitmap.

    Return Values:

        The previous value of the bitmap bit.

--*/

{
    size_t q = bit >> 6;
    size_t b = bit & 63;
    uint64_t mask = 1ULL << b;
    uint64_t old_qword = __sync_fetch_and_and(&bitmap[q], ~mask);

    return (old_qword & mask) != 0;
}

FORCEINLINE
uintptr_t
MiIndexToVa(
    uintptr_t poolBase,
    size_t index
)

// Converts a pool base index to its corresponding virtual address.

/*++

    Routine description:

        Converts a kernel virtual-address bitmap index to an address.

    Arguments:

        [IN] poolBase - Base virtual address represented by bitmap index zero.
        [IN] index - Index of the entry to process.

    Return Values:

        The virtual address represented by the bitmap index.

--*/

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

/*++

    Routine description:

        Converts a kernel virtual address to its bitmap index.

    Arguments:

        [IN] poolBase - Base virtual address represented by bitmap index zero.
        [IN] va - Virtual address whose paging index or entry is requested.

    Return Values:

        The bitmap index represented by the virtual address.

--*/

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
        [IN] size_t NumberOfBytes - The amount of contingious VA bytes to find for. (rounds up to next page)

    Return Values:

        The VA on success, otherwise 0 on failure.

    Notes:

        The returned VA is not mapped to any physical memory.

--*/

{
    // Declarations for mixed pools
    size_t total_pages, total_qwords;
    size_t hint;
    uint64_t* bitmap;
    uintptr_t poolBase;
    volatile uint64_t* hintIndexPtr;

    // Calculate pages needed without allowing the round-up to wrap.
    if (NumberOfBytes == 0 ||
        NumberOfBytes > SIZE_MAX - (VirtualPageSize - 1)) {
        return 0;
    }

    size_t NumberOfPages = BYTES_TO_PAGES(NumberOfBytes);

    // Set-up pool specific parameters.
    if (PoolType == NonPagedPool || PoolType == NonPagedPoolNx) {
        total_pages = NONPAGED_POOL_VA_TOTAL_PAGES;
        hint = (size_t)InterlockedFetchU64(&g_NonpagedPoolHintIndex);
        bitmap = g_NonpagedPoolVaBitmap;
        poolBase = MI_NONPAGED_POOL_BASE;
        hintIndexPtr = &g_NonpagedPoolHintIndex;
    }
    else if (PoolType == PagedPool) {
        total_pages = PAGED_POOL_VA_TOTAL_PAGES;
        hint = (size_t)InterlockedFetchU64(&g_PagedPoolHintIndex);
        bitmap = g_PagedPoolVaBitmap;
        poolBase = MI_PAGED_POOL_BASE;
        hintIndexPtr = &g_PagedPoolHintIndex;
    }
    else {
        // Invalid parameter.
        return 0;
    }

    if (!bitmap || NumberOfPages > total_pages) return 0;

    total_qwords = (total_pages + 63) / 64;

    // SINGLE PAGE ALLOCATION
    if (NumberOfPages == 1) {
        size_t start_q = (hint / 64) % total_qwords;

        // Scan qword-by-qword
        for (size_t i = 0; i < total_qwords; i++) {
            size_t q_idx = (start_q + i) % total_qwords;

            // rescan loop
            while (true)
            {
                uint64_t qword = InterlockedFetchU64(
                    (volatile uint64_t*)&bitmap[q_idx]
                );
                if (qword == 0xFFFFFFFFFFFFFFFFULL) {
                    break; // This qword is full, move to the next q_idx
                }

                uint64_t inverted_qword = ~qword;
                unsigned long bit_index_in_qword = __builtin_ctzll(inverted_qword);
                size_t global_bit_idx = (q_idx * 64) + bit_index_in_qword;

                // The final bitmap qword may contain padding bits.
                if (global_bit_idx >= total_pages) break;

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
                        (void)MiBitmapClearBitLocked(bitmap, start_of_run_idx + k);
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
    size_t NumberOfPages;
    size_t total_pages;
    uint64_t* bitmap;
    uintptr_t poolBase;
    uintptr_t poolEnd;

    if (NumberOfBytes == 0 ||
        NumberOfBytes > SIZE_MAX - (VirtualPageSize - 1)) {
        MeBugCheckEx(MEMORY_INVALID_FREE, (void*)va,
                     (void*)(uintptr_t)NumberOfBytes, RETADDR(0), NULL);
    }

    NumberOfPages = BYTES_TO_PAGES(NumberOfBytes);

    if (PoolType == NonPagedPool || PoolType == NonPagedPoolNx) {
        poolBase = MI_NONPAGED_POOL_BASE;
        poolEnd = MI_NONPAGED_POOL_END;
        bitmap = g_NonpagedPoolVaBitmap;
        total_pages = NONPAGED_POOL_VA_TOTAL_PAGES;
    }
    else if (PoolType == PagedPool) {
        poolBase = MI_PAGED_POOL_BASE;
        poolEnd = MI_PAGED_POOL_END;
        bitmap = g_PagedPoolVaBitmap;
        total_pages = PAGED_POOL_VA_TOTAL_PAGES;
    }
    else {
        MeBugCheckEx(MEMORY_INVALID_FREE, (void*)va,
                     (void*)(uintptr_t)PoolType, RETADDR(0), NULL);
    }

    if (!bitmap || va < poolBase || va >= poolEnd ||
        ((va - poolBase) & (VirtualPageSize - 1)) != 0) {
        MeBugCheckEx(MEMORY_INVALID_FREE, (void*)va,
                     (void*)poolBase, (void*)poolEnd, RETADDR(0));
    }

    size_t start_idx = MiVaToIndex(poolBase, va);

    if (NumberOfPages > total_pages ||
        start_idx > total_pages - NumberOfPages) {
        MeBugCheckEx(MEMORY_INVALID_FREE, (void*)va,
                     (void*)(uintptr_t)NumberOfPages,
                     (void*)(uintptr_t)total_pages, RETADDR(0));
    }

    // Validate the entire allocation before changing the bitmap. This turns a
    // bad size or ordinary double-free into a fault at the actual caller.
    for (size_t i = 0; i < NumberOfPages; i++) {
        if (!MiBitmapTestBit(bitmap, start_idx + i)) {
            MeBugCheckEx(MEMORY_INVALID_FREE, (void*)va,
                         (void*)(uintptr_t)(start_idx + i),
                         (void*)(uintptr_t)NumberOfPages, RETADDR(0));
        }
    }

    // Loop and free all bits in the range
    for (size_t i = 0; i < NumberOfPages; i++) {
        if (!MiBitmapClearBitLocked(bitmap, start_idx + i)) {
            // A concurrent second free raced the validation above.
            MeBugCheckEx(MEMORY_INVALID_FREE, (void*)va,
                         (void*)(uintptr_t)(start_idx + i),
                         (void*)(uintptr_t)NumberOfPages, RETADDR(0));
        }
    }
}
