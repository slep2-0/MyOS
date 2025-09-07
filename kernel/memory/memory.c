/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     Memory Management Implementation
 */
#include "memory.h"
#include "../drivers/gop/gop.h"
#include "../bugcheck/bugcheck.h"
#include "../assert.h"

 /* Head of the free list */
static BLOCK_HEADER* free_list = NULL;
extern GOP_PARAMS gop_local;
uintptr_t heap_current_end;

static SPINLOCK heap_lock = { 0 };

/// <summary>
/// Zeroes out the BSS section (garbage data that might have left there)
/// </summary>
/// <remarks>
/// The function takes the start and end address of the `.bss` section (provided by the linker script),
/// and iterates over each byte from the start address up to (but not including) the end address,
/// writing zero to each location. This ensures all global/static variables without initializers are zeroed.
/// </remarks>
void zero_bss(void) {
    tracelast_func("zero_bss");
    assert(&bss_start < &bss_end, "bss_start < bss_end");
    uint8_t* p = &bss_start;
    while (p < &bss_end) *p++ = 0;
}

/// <summary>
/// Initializes the Kernel's HEAP for dynamic memory allocation.
/// </summary>
/// <remarks>
/// The function declares the globals for the heap current end, as well as setting up the free_list pointer.
/// It first creates 1 initial 4KiB frame, maps it to be paged in virtual memory, and increases the heap current end by the frame size (4KiB), so that 1 starting page is allocated.
/// </remarks>
void init_heap(void) {
    tracelast_func("init_heap");
    spinlock_init(&heap_lock);
    // map that frame:
    uintptr_t phys = alloc_frame();
    map_page((void*)HEAP_START, phys, PAGE_PRESENT | PAGE_RW);

    // Zero the whole new page first, then initialize header fields.
    kmemset((void*)HEAP_START, 0, FRAME_SIZE);

    free_list = (BLOCK_HEADER*)HEAP_START;
    assert(((uintptr_t)free_list & (sizeof(BLOCK_HEADER) - 1)) == 0, "free_list alignment");
    assert(FRAME_SIZE >= sizeof(BLOCK_HEADER), "FRAME_SIZE >= header");
    heap_current_end = HEAP_START;
    assert(heap_current_end == HEAP_START, "heap_current_end initialized");


    free_list->magic = HEADER_MAGIC;
    free_list->block_size = FRAME_SIZE;
    free_list->next = NULL;
    free_list->in_use = false;
    free_list->kind = 0;

    heap_current_end += FRAME_SIZE;
}

/// <summary>
/// IRQL Requirement: DISPATCH_LEVEL or below.
/// 
/// The function inserts the "newblock" parameter into the free_list in ascending order.
/// </summary>
/// <param name="newblock">The pointer to the new block to insert in ascending order e.g: (A -> A[A + 0x1000] -> NULL (or A[A + 0x2000])</param>
static void insert_block_sorted(BLOCK_HEADER* newblock) {
    tracelast_func("insert_block_sorted");
    assert(newblock != NULL, "newblock != NULL");
    assert(newblock->magic == HEADER_MAGIC, "newblock magic");
    assert(newblock->block_size >= sizeof(BLOCK_HEADER), "newblock size >= header");
    assert((uintptr_t)newblock >= HEAP_START && (uintptr_t)newblock < heap_current_end + FRAME_SIZE,
        "newblock in heap range");
    uint64_t rip;
    GET_RIP(rip);
    enforce_max_irql(DISPATCH_LEVEL, (void*)rip);
    if (!free_list || newblock < free_list) {
        newblock->next = free_list;
        free_list = newblock;
        return;
    }

    BLOCK_HEADER* current = free_list;
    assert(current && ((uintptr_t)current >= HEAP_START && (uintptr_t)current < heap_current_end),
        "current in heap");

    while (current->next && current->next < newblock) {
        current = current->next;
    }

    newblock->next = current->next;
    current->next = newblock;
}

/* Merge adjacent free blocks to reduce fragmentation */
static void coalesce_free_list(void) {
    tracelast_func("coalesce_free_list");
    uint64_t rip;
    GET_RIP(rip);
    enforce_max_irql(DISPATCH_LEVEL, (void*)rip);
    BLOCK_HEADER* b = free_list;
    while (b && b->next) {
        assert(b->magic == HEADER_MAGIC, "coalesce: b magic");
        assert(b->block_size >= sizeof(BLOCK_HEADER), "coalesce: b size >= header");
        uintptr_t end_of_b = (uintptr_t)b + b->block_size;
        assert(end_of_b > (uintptr_t)b, "coalesce: end_of_b overflow");

        assert((uintptr_t)b->next >= HEAP_START && (uintptr_t)b->next < heap_current_end,
            "coalesce: next in heap");

        if (end_of_b == (uintptr_t)b->next) {
            BLOCK_HEADER* consumed_block = b->next;
            b->block_size += consumed_block->block_size;
            b->next = consumed_block->next;

            // **FIX:** Scrub the header of the consumed block to prevent misuse of stale data.
            kmemset(consumed_block, 0, sizeof(BLOCK_HEADER));

            // Do NOT advance 'b' here; the newly enlarged block might merge with the next one.
        }
        else {
            // Not adjacent, so we can safely move to the next block.
            b = b->next;
        }
    }
}

static bool grow_heap_by_one_page(void) {
    tracelast_func("grow_heap_by_one_page");
    uint64_t rip;
    GET_RIP(rip);
    enforce_max_irql(DISPATCH_LEVEL, (void*)rip);

    uintptr_t phys = alloc_frame();
    assert(phys != 0, "alloc_frame returned 0");
    if (!phys) { return false; }

    map_page((void*)heap_current_end, phys, PAGE_PRESENT | PAGE_RW);

    kmemset((void*)heap_current_end, 0, FRAME_SIZE);
    assert(((uintptr_t)heap_current_end & (FRAME_SIZE - 1)) == 0, "heap_current_end page-aligned");

    uintptr_t new_block_addr = heap_current_end;
    heap_current_end += FRAME_SIZE; 

    BLOCK_HEADER* block = (BLOCK_HEADER*)new_block_addr;
    block->magic = HEADER_MAGIC;
    block->block_size = FRAME_SIZE;
    block->next = NULL;
    block->in_use = false;
    block->kind = 0;
    assert(block->magic == HEADER_MAGIC, "new block magic");
    assert(block->block_size == FRAME_SIZE, "new block size == FRAME_SIZE");

    insert_block_sorted(block);
    coalesce_free_list();

    return true;
}



// Memory Set.
void* kmemset(void* dest, int64_t val, uint64_t len) {
    tracelast_func("kmemset");
    assert(dest != NULL, "kmemset: dest != NULL");
    assert(len == 0 || ((uintptr_t)dest + len) > (uintptr_t)dest, "kmemset: no wrap");
    uint64_t rip;
    GET_RIP(rip);
    enforce_max_irql(DISPATCH_LEVEL, (void*)rip);
    uint8_t* ptr = dest;
    for (size_t i = 0; i < (size_t)len; i++) {
        ptr[i] = (uint8_t)val;
    }
    return dest;
}

// Memory copy  
void* kmemcpy(void* dest, const void* src, size_t len) {
    tracelast_func("kmemcpy");
    assert(dest != NULL && src != NULL, "kmemcpy: non-null pointers");
    assert(len == 0 || ((uintptr_t)dest + len) > (uintptr_t)dest, "kmemcpy: dest wrap");
    assert(len == 0 || ((uintptr_t)src + len) > (uintptr_t)src, "kmemcpy: src wrap");
    uint64_t rip;
    GET_RIP(rip);
    enforce_max_irql(DISPATCH_LEVEL, (void*)rip);
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < len; i++) d[i] = s[i];
    return dest;
}

/* safer align_up */
static inline uintptr_t align_up_uintptr(uintptr_t v, size_t a) {
    assert(a != 0);
    uintptr_t rem = v % a;
    if (rem == 0) return v;
    return v + (a - rem);
}

/// <summary>
/// Allocates a robust, verified block of memory from the kernel's memory manager.
/// This new implementation uses a header/footer canary system to detect buffer overflows.
/// </summary>
void* MtAllocateVirtualMemory(size_t wanted_size, size_t align) {
    tracelast_func("MtAllocateVirtualMemory");
    assert(align != 0 && (align & (align - 1)) == 0, "align must be power-of-two");
    assert(wanted_size > 0 && wanted_size <= (SIZE_MAX / 2), "wanted_size sane");

    IRQL oldIrql;
    MtAcquireSpinlock(&heap_lock, &oldIrql);

    // Ensure at least pointer-alignment to safely store header pointer
    if (align < sizeof(void*)) align = sizeof(void*);

    // The minimum size of a split-off free block must hold a header and a footer.
    const size_t min_free_block_size = sizeof(BLOCK_HEADER) + sizeof(BLOCK_FOOTER);

    for (;;) {
        BLOCK_HEADER** cur = &free_list;
        while (*cur) {
            BLOCK_HEADER* blk = *cur;
            assert(blk->magic == HEADER_MAGIC && !blk->in_use, "Corrupted free list entry");

            // Calculate the total memory footprint for this allocation request.
            // This includes the header, padding for alignment, user data, and the footer canary.
            uintptr_t data_start_potential = (uintptr_t)(blk + 1);
            uintptr_t header_ptr_storage = data_start_potential; // We will store the header pointer right after the header
            uintptr_t user_ptr_potential = align_up_uintptr(header_ptr_storage + sizeof(void*), align);
            uintptr_t footer_ptr_potential = user_ptr_potential + wanted_size;
            uintptr_t end_of_alloc_potential = footer_ptr_potential + sizeof(BLOCK_FOOTER);

            size_t total_needed = end_of_alloc_potential - (uintptr_t)blk;

            // Check if this block is large enough
            if (blk->block_size < total_needed) {
                cur = &blk->next;
                continue;
            }

            // This block is suitable. Decide whether to split it or use it whole.
            size_t remaining_size = blk->block_size - total_needed;

            if (remaining_size >= min_free_block_size) {
                // SPLIT THE BLOCK
                BLOCK_HEADER* new_free_blk = (BLOCK_HEADER*)((uintptr_t)blk + total_needed);
                new_free_blk->magic = HEADER_MAGIC;
                new_free_blk->in_use = false;
                new_free_blk->block_size = remaining_size;
                new_free_blk->kind = 0;
                new_free_blk->requested_size = 0;

                // The new free block takes the old one's place in the list.
                new_free_blk->next = blk->next;
                *cur = new_free_blk;

                // Shrink the original block, which we are about to allocate.
                blk->block_size = total_needed;
            }
            else {
                // USE THE WHOLE BLOCK
                // Unlink 'blk' from the free list.
                *cur = blk->next;
            }

            // Finalize the allocated block 'blk'
            blk->in_use = true;
            blk->kind = BLK_NORMAL;
            blk->next = NULL;
            blk->requested_size = wanted_size;

            // Get the final user pointer and footer pointer
            void* user_ptr = (void*)user_ptr_potential;
            BLOCK_FOOTER* footer = (BLOCK_FOOTER*)footer_ptr_potential;
            footer->magic = FOOTER_MAGIC;

            // Store the back-pointer to the header for freeing.
            kmemcpy((void*)(user_ptr_potential - sizeof(void*)), &blk, sizeof(blk));

            // Zero the user area
            kmemset(user_ptr, 0, wanted_size);

            MtReleaseSpinlock(&heap_lock, oldIrql);
            return user_ptr;
        }

        // No suitable block found, grow the heap and try again.
        size_t pages_to_grow = (wanted_size + sizeof(BLOCK_HEADER) + sizeof(BLOCK_FOOTER) + align + FRAME_SIZE - 1) / FRAME_SIZE;
        for (size_t i = 0; i < pages_to_grow; i++) {
            if (!grow_heap_by_one_page()) {
                MtReleaseSpinlock(&heap_lock, oldIrql);
                MtBugcheck(NULL, NULL, MEMORY_LIMIT_REACHED, 0, false);
                return NULL; // Unreachable
            }
        }
    }
}

bool MtIsHeapAddressAllocated(void* ptr) {
    assert(ptr != NULL, "MtIsHeapAddressAllocated: ptr != NULL");
    if (!ptr) return false;
    uintptr_t p = (uintptr_t)ptr;
    assert(p >= HEAP_START && p < heap_current_end, "MtIsHeapAddressAllocated: ptr in heap");
    assert(p - sizeof(void*) >= HEAP_START, "MtIsHeapAddressAllocated: header_store bounds");

    // Basic heap bounds check
    if (p < HEAP_START || p >= heap_current_end) return false;

    // Ensure we have enough space for the header pointer
    if (p < HEAP_START + sizeof(void*)) return false;

    // Get the stored header pointer with bounds checking
    uintptr_t header_store_addr = p - sizeof(void*);

    // Validate header_store_addr is within heap bounds
    if (header_store_addr < HEAP_START || header_store_addr >= heap_current_end) {
        return false;
    }

    BLOCK_HEADER* header = NULL;
    uintptr_t header_store_addrz = (uintptr_t)ptr - sizeof(void*);
    kmemcpy(&header, (void*)header_store_addrz, sizeof(header));

    // Validate header pointer is not null and within heap bounds
    if (!header) return false;
    if ((uintptr_t)header < HEAP_START || (uintptr_t)header >= heap_current_end) {
        return false;
    }

    // Validate header magic and size
    if (header->magic != HEADER_MAGIC) return false;
    if (header->block_size < sizeof(BLOCK_HEADER) || header->block_size >(heap_current_end - HEAP_START)) {
        return false;
    }

    // Validate that the header points to a valid block that contains our pointer
    uintptr_t block_start = (uintptr_t)header;
    uintptr_t block_end = block_start + header->block_size;

    if (p < block_start || p >= block_end) {
        return false;
    }

    return header->in_use;
}

void* MtAllocateVirtualMemoryEx(size_t wanted_size, size_t align, uint64_t flags) {
    tracelast_func("MtAllocateVirtualMemoryEx");

    if (align == 0 || (align & (align - 1)) != 0) return NULL;
    if (align < sizeof(void*)) align = sizeof(void*);

    // 1. Calculate the total size needed, including our header and alignment padding.
    size_t header_size = sizeof(BLOCK_HEADER) + sizeof(void*); // For header and original pointer
    size_t total_size = wanted_size + header_size + (align - 1);

    // 2. Round this up to the nearest page size (4K)
    size_t pages_needed = (total_size + FRAME_SIZE - 1) / FRAME_SIZE;
    size_t region_size = pages_needed * FRAME_SIZE;

    // 3. Map a new, contiguous region of memory with the specified flags.
    //    We will map it at the end of the current heap.
    void* region_start_virt = (void*)heap_current_end;

    for (size_t i = 0; i < pages_needed; i++) {
        uintptr_t phys = alloc_frame();
        if (!phys) {
            CTX_FRAME ctx;
            SAVE_CTX_FRAME(&ctx);
            MtBugcheck(&ctx, NULL, MEMORY_LIMIT_REACHED, 0, false);
        }
        void* va = (uint8_t*)region_start_virt + (i * FRAME_SIZE);
        map_page(va, phys, flags);
    }

    heap_current_end += region_size; // Claim the virtual address space immediately

    // 4. Set up the block header at the start of our new region.
    BLOCK_HEADER* blk = (BLOCK_HEADER*)region_start_virt;
    blk->magic = HEADER_MAGIC;
    blk->block_size = region_size;
    blk->next = NULL;
    blk->in_use = true;
    blk->kind = BLK_EX;

    assert(((uintptr_t)region_start_virt & (FRAME_SIZE - 1)) == 0, "Ex region page aligned");
    assert(blk->magic == HEADER_MAGIC, "Ex blk magic");
    assert(blk->block_size == region_size, "Ex blk size");

    // 5. Perform the same alignment logic as before to get the final user pointer.
    uintptr_t data_start = (uintptr_t)(blk + 1);
    uintptr_t user_start = data_start + sizeof(void*);
    uintptr_t aligned_start = (user_start + align - 1) & ~(align - 1);
    uintptr_t header_store = aligned_start - sizeof(void*);

    assert(header_store >= data_start && (header_store + sizeof(void*)) <= (uintptr_t)blk + region_size,
        "Ex header_store in region");

    /* Safety check: header_store must be inside the region and after the header */
    if (header_store < data_start || (header_store + sizeof(void*)) >(uintptr_t)blk + region_size) {
        CTX_FRAME ctx;
        SAVE_CTX_FRAME(&ctx);
        MtBugcheck(&ctx, NULL, MEMORY_CORRUPT_HEADER, 0, false);
    }

    /* Store the original block pointer for the free function. */
    BLOCK_HEADER* _tmp_hdr = blk;
    kmemcpy((void*)header_store, &_tmp_hdr, sizeof(_tmp_hdr));

    kmemset((void*)aligned_start, 0, wanted_size);
    return (void*)aligned_start;
}

/// <summary>
/// Releases (frees) a previously allocated block of memory.
/// This new implementation validates a header and footer canary to detect
/// corruption before modifying the heap, preventing crashes.
/// </summary>
void MtFreeVirtualMemory(void* ptr) {
    if (!ptr) return;

    IRQL oldIrql;
    MtAcquireSpinlock(&heap_lock, &oldIrql);
    tracelast_func("MtFreeVirtualMemory");

    uintptr_t p = (uintptr_t)ptr;

    // --- Stage 1: Basic Pointer Validation ---
    if (p < HEAP_START || p >= heap_current_end) {
        MtReleaseSpinlock(&heap_lock, oldIrql);
        MtBugcheck(NULL, NULL, MEMORY_INVALID_FREE, 1, false);
        return;
    }

    // --- Stage 2: Retrieve and Validate Header ---
    uintptr_t header_store_addr = p - sizeof(void*);
    BLOCK_HEADER* blk = NULL;
    kmemcpy(&blk, (void*)header_store_addr, sizeof(blk));

    if (!blk || (uintptr_t)blk < HEAP_START || (uintptr_t)blk >= heap_current_end) {
        MtReleaseSpinlock(&heap_lock, oldIrql);
        MtBugcheck(NULL, NULL, MEMORY_CORRUPT_HEADER, 2, false);
        return;
    }
    if (blk->magic != HEADER_MAGIC) {
        MtReleaseSpinlock(&heap_lock, oldIrql);
        MtBugcheck(NULL, NULL, MEMORY_CORRUPT_HEADER, 3, false);
        return;
    }
    if (!blk->in_use) {
        MtReleaseSpinlock(&heap_lock, oldIrql);
        MtBugcheck(NULL, NULL, MEMORY_DOUBLE_FREE, 4, false);
        return;
    }

    // --- Stage 3: Validate Footer Canary (Detect Buffer Overflow) ---
    // This is the most important new safety check.
    if (blk->kind == BLK_NORMAL) {
        uintptr_t footer_addr = p + blk->requested_size;
        BLOCK_FOOTER* footer = (BLOCK_FOOTER*)footer_addr;

        // Check that the footer itself is within the block's claimed boundaries
        if (footer_addr + sizeof(BLOCK_FOOTER) > (uintptr_t)blk + blk->block_size) {
            MtReleaseSpinlock(&heap_lock, oldIrql);
            MtBugcheck(NULL, NULL, MEMORY_CORRUPT_HEADER, 5, false); // Header size is inconsistent
            return;
        }

        if (footer->magic != FOOTER_MAGIC) {
            MtReleaseSpinlock(&heap_lock, oldIrql);
            // The footer canary was destroyed, indicating a buffer overflow.
            MtBugcheck(NULL, NULL, MANUALLY_INITIATED_CRASH, 6, false);
            return;
        }
    }

    // --- Stage 4: Deallocation ---
    // All checks passed. The block metadata is considered valid.

    if (blk->kind == BLK_EX) {
        // ... (The BLK_EX logic remains largely the same as it doesn't use the normal heap) ...
        // (ensure you are using blk->block_size here instead of a local 'region_size')
        size_t pages_to_unmap = blk->block_size / FRAME_SIZE;
        uintptr_t region_start = (uintptr_t)blk;
        for (size_t i = 0; i < pages_to_unmap; i++) {
            unmap_page((void*)(region_start + (i * FRAME_SIZE)));
        }
        if ((region_start + blk->block_size) == heap_current_end) {
            heap_current_end -= blk->block_size;
        }
    }
    else {
        // For normal blocks, poison the memory and add to free list.
        kmemset(ptr, 0, blk->requested_size); // Zero user data
        blk->in_use = false;
        blk->requested_size = 0;

        // Poison header and footer to catch use-after-free bugs
        blk->magic = ~HEADER_MAGIC;
        BLOCK_FOOTER* footer = (BLOCK_FOOTER*)(p + blk->requested_size);
        footer->magic = ~FOOTER_MAGIC;

        // Restore magic for free list operations
        blk->magic = HEADER_MAGIC;

        insert_block_sorted(blk);
        coalesce_free_list();
    }

    MtReleaseSpinlock(&heap_lock, oldIrql);
}