/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     Memory Management Implementation
 */
#include "memory.h"
#include "../drivers/gop/gop.h"
#include "../bugcheck/bugcheck.h"

/// As my understanding of the memory fades with time, I will leave thorough comments for reminders.

/* Head of the free list */
static BLOCK_HEADER* free_list = NULL;
extern GOP_PARAMS gop_local;
uintptr_t heap_current_end;

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
    heap_current_end = HEAP_START;
    free_list = (BLOCK_HEADER*)HEAP_START;
    free_list->size = FRAME_SIZE;    // only 4 KiB initially
    free_list->next = NULL;

    // map that frame:
    void* phys = alloc_frame();
    map_page((void*)HEAP_START, phys, PAGE_PRESENT | PAGE_RW);
    kmemset((void*)HEAP_START, 0, FRAME_SIZE);
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
    enforce_max_irql(DISPATCH_LEVEL);
    if (!free_list || newblock < free_list) {
        newblock->next = free_list;
        free_list = newblock;
        return;
    }

    BLOCK_HEADER* current = free_list;
    while (current->next && current->next < newblock) {
        current = current->next;
    }

    newblock->next = current->next;
    current->next = newblock;
}

// Functions.
static bool grow_heap_by_one_page(void) {
    tracelast_func("grow_heap_by_one_page");
    enforce_max_irql(DISPATCH_LEVEL);
    // grab a physical frame
    void* phys = alloc_frame();
    if (!phys) { return false; }

    // map it at the end of the heap.
    // here it where it maps the page, I was confused on how it worked, forgot about it.
    map_page((void*)heap_current_end, phys, PAGE_PRESENT | PAGE_RW /* | PAGE_USER Would be used later on, when this kernel has both user mode and kernel mode */ );
    
    // Zero the page.
    kmemset((void*)heap_current_end, 0, FRAME_SIZE);
    // insert a new region
    BLOCK_HEADER* block = (BLOCK_HEADER*)heap_current_end;
    block->size = FRAME_SIZE;
    insert_block_sorted(block);
    coalesce_free_list();

    // advance our end.
    heap_current_end += FRAME_SIZE;
    return true;
}

// unused.
/* Align `addr` up to the next multiple of `align` (align must be power of two) */
static void* align_up(void* addr, size_t align) {
    tracelast_func("align_up - memory");
    enforce_max_irql(DISPATCH_LEVEL);
    uintptr_t a = (uintptr_t)addr;
    uintptr_t mask = align - 1;
    if (a & mask) {
        a = (a + mask) & ~mask;
    }
    return (void*)a;
}

// Memory Set.
void* kmemset(void* dest, int val, uint32_t len) {
    tracelast_func("kmemset");
    enforce_max_irql(DISPATCH_LEVEL);
    uint8_t* ptr = dest;
    for (uint32_t i = 0; i < len; i++) {
        ptr[i] = (uint8_t)val;
    }
    return dest;
}

// Memory copy.
void* kmemcpy(void* dest, const void* src, uint32_t len) {
    tracelast_func("kmemcpy");
    enforce_max_irql(DISPATCH_LEVEL);
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    for (unsigned int i = 0; i < len; i++) {
        d[i] = s[i];
    }
    return dest;
}

/// <summary>
/// Allocate `size` bytes, aligned to `align` bytes.
/// </summary>
/// <param name="wanted_size">Amount of size to allocate memory in BYTES.</param>
/// <param name="align">Alignment, align by your struct header (using alignof) OR use a normal one (e.g 64,128)</param>
/// <returns></returns>
void* kmalloc(size_t wanted_size, size_t align) {
    tracelast_func("kmalloc");
    enforce_max_irql(DISPATCH_LEVEL);
    /* Round up the requested size to satisfy alignment of payload */
    size_t payload_size = (wanted_size + align - 1) & ~(align - 1);
    size_t total_size = payload_size + sizeof(BLOCK_HEADER);
    
    // Infinite loop, either allocate the required memory, or bugcheck that we have ran out.
    for (;;) {
        // 1 - First fit search.
        BLOCK_HEADER** cur = &free_list;
        while (*cur) {
            BLOCK_HEADER* blk = *cur;

            if (blk->size >= total_size) {
                /* If the block is big enough to split */
                if (blk->size >= total_size + sizeof(BLOCK_HEADER) + align) {
                    /* Split off the tail into a new free block */
                    BLOCK_HEADER* next_blk = (BLOCK_HEADER*)((uintptr_t)blk + total_size);
                    next_blk->size = blk->size - total_size;
                    next_blk->next = blk->next;

                    /* Shrink current block to allocated size */
                    blk->size = total_size;
                    *cur = next_blk;
                }
                else {
                    /* Use the entire block */
                    *cur = blk->next;
                }

                void* raw_ptr = (void*)(blk + 1);
                void* aligned_ptr = align_up((void*)((uintptr_t)raw_ptr + sizeof(void*)), align);
                ((BLOCK_HEADER**)aligned_ptr)[-1] = blk;
                return aligned_ptr;
            }

            cur = &blk->next;
        }

        // 2 - No available block is found, grow by how many we need.
        size_t pages_needed = (total_size + FRAME_SIZE - 1) / FRAME_SIZE; // Round up.
        for (size_t i = 0; i < pages_needed; i++) {
            if (!grow_heap_by_one_page()) {
                bugcheck_system(NULL, NULL, MEMORY_LIMIT_REACHED, 0, false);
            }
        }
        // We have grown the amount, retry.
        continue;
    }
}

/* Merge adjacent free blocks to reduce fragmentation */
void coalesce_free_list(void) {
    tracelast_func("coalesce_free_list");
    enforce_max_irql(DISPATCH_LEVEL);
    BLOCK_HEADER* b = free_list;
    while (b && b->next) {
        uintptr_t end_of_b = (uintptr_t)b + b->size;
        if (end_of_b == (uintptr_t)b->next) {
            /* Adjacent: merge */
            b->size += b->next->size;
            b->next = b->next->next;
        }
        else {
            b = b->next;
        }
    }
}

/* Return a block to the free list */
void kfree(void* ptr) {
    tracelast_func("kfree");
    enforce_max_irql(DISPATCH_LEVEL);
    if (!ptr) {
#ifdef DEBUG
        gop_printf(&gop_local, 0xFFFF0000, "<-- KFREE() DEBUG --> nullptr passed as argument, returning\n");
#endif
        return;
    }
    /* Get header */
    BLOCK_HEADER* blk = ((BLOCK_HEADER**)ptr)[-1];
   
#ifdef DEBUG
    gop_printf(&gop_local, 0xFFFFFF00, "<-- KFREE() DEBUG --> blk: %p\n", blk);
#endif
    /* Zero it out. */
    // FATAL ERROR BEFORE, it zeroed out the block metadata as well, so kmalloc didn't even know it existed.
    // The fix is to skip over the metadata, just zero the payload (actual data) (minus the actual block value, so we don't zero out the next 8 bytes in the next block)
    // REMEMBER TO MYSELF: pointer arithmetic in C and C++ moves the pointer BASED ON THE TYPE OF IT! (so void ptr in 32 bit is move 4 bytes, or char which is move 1 byte, and here BLOCK_HEADER is 8 bytes, (and technically we just want to move 1 block header so we skip over it's metadata))
    kmemset((void*)(blk + 1), 0, blk->size - sizeof(BLOCK_HEADER));
    /* Push it onto the free list */
    insert_block_sorted(blk);
#ifdef DEBUG
    gop_printf(&gop_local, 0xFFFFFF00 ,"<-- KFREE() DEBUG --> ");
    gop_printf(&gop_local, 0xFF00FFFF, "Pushed the block to the free list.\n");
#endif
    /* Optionally merge neighbors */
    coalesce_free_list();
}
