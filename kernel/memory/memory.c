/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     Memory Management Implementation
 */
#include "memory.h"

/* Head of the free list */
static BLOCK_HEADER* free_list = NULL;

/* Initialize the free list to one big block spanning the heap */
void init_heap(void) {
    heap_current_end = HEAP_START;
    uintptr_t start = HEAP_START;
    size_t total = HEAP_SIZE;

    /* Carve out one big free block */
    free_list = (BLOCK_HEADER*)start;
    free_list->size = total;
    free_list->next = NULL;
}


// Functions.
static bool grow_heap_by_one_page(void) {
    // grab a physical frame
    void* phys = alloc_frame();
    if (!phys) { return false; }

    // map it at the end of the heap.
    map_page((void*)heap_current_end, phys, PAGE_PRESENT | PAGE_RW /* | PAGE_USER Would be used later on, when this kernel has both user mode and kernel mode */ );
    // Zero the page.
    kmemset((void*)heap_current_end, 0, FRAME_SIZE);
    
    // insert a new region
    BLOCK_HEADER* block = (BLOCK_HEADER*)heap_current_end;
    block->size = FRAME_SIZE;
    block->next = free_list;
    free_list = block;
    coalesce_free_list();

    // advance our end.
    heap_current_end += FRAME_SIZE;
    return true;
}

void insert_block_sorted(BLOCK_HEADER* newblock) {
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

// unused.
/* Align `addr` up to the next multiple of `align` (align must be power of two) */
static void* align_up(void* addr, size_t align) {
    uintptr_t a = (uintptr_t)addr;
    uintptr_t mask = align - 1;
    if (a & mask) {
        a = (a + mask) & ~mask;
    }
    return (void*)a;
}

// Memory Set.
void* kmemset(void* dest, int val, uint32_t len) {
    uint8_t* ptr = dest;
    for (uint32_t i = 0; i < len; i++) {
        ptr[i] = (uint8_t)val;
    }
    return dest;
}

/* Allocate `size` bytes, aligned to `align` bytes */
void* kmalloc(size_t wanted_size, size_t align) {
    /* Round up the requested size to satisfy alignment of payload */
    size_t payload_size = (wanted_size + align - 1) & ~(align - 1);
    size_t total_size = payload_size + sizeof(BLOCK_HEADER);

    /* First‑fit search through free list */
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

            /* Return pointer just past the header */
            return (void*)(blk + 1);
        }

        cur = &blk->next;
    }
    if (!*cur) {
        // Out of memory, add new page maps.
        if (heap_current_end + FRAME_SIZE <= HEAP_END && grow_heap_by_one_page()) {
            // new block now, restart the kmalloc.
            kmalloc(wanted_size, align);
        }
    }
    // No physical memory left.
    return NULL;
}

/* Merge adjacent free blocks to reduce fragmentation */
void coalesce_free_list(void) {
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
    if (!ptr) {
#ifdef DEBUG
        print_to_screen("<-- KFREE() DEBUG --> nullptr passed as argument, returning\r\n", COLOR_RED);
#endif
        return;
    }
    /* Get header */
    BLOCK_HEADER* blk = ((BLOCK_HEADER*)ptr) - 1;
    
#ifdef DEBUG
    print_to_screen("<-- KFREE() DEBUG --> ", COLOR_YELLOW);
    print_to_screen("blk: ", COLOR_CYAN);
    print_hex((unsigned int)(uintptr_t)blk, COLOR_BROWN);
    print_to_screen("\r\n", COLOR_BLACK);
#endif
    /* Zero it out. */
    kmemset(blk, 0, blk->size);
    /* Push it onto the free list */
    insert_block_sorted(blk);
#ifdef DEBUG
    print_to_screen("<-- KFREE() DEBUG --> ", COLOR_YELLOW);
    print_to_screen("Pushed the block to the free list.\r\n", COLOR_CYAN);
#endif
    /* Optionally merge neighbors */
    coalesce_free_list();
}
