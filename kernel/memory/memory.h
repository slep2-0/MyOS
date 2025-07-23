/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     Memory Management Header
 */

#ifndef MEMORY_H
#define MEMORY_H
#include "../kernel.h"

 /* Symbol defined in linker script, end of loaded kernel */
extern char kernel_end;
extern char kernel_start;
extern const size_t kernel_length;

/* Zero Out BSS */
extern uint8_t bss_start;
extern uint8_t bss_end;

void zero_bss(void);

/* Heap Size */

/* Start and end of the heap region */
#define HEAP_START ((uintptr_t)((((uintptr_t)&kernel_end + 0xFFFU) & ~0xFFFU)))
#define HEAP_END (PHYS_MEM_BASE + PHYS_MEM_SIZE)
uintptr_t heap_current_end;

#define HEAP_SIZE (HEAP_END - HEAP_START) // This ensures heap stays within mapped range // 128 MiB MAX. You may change it to an upper limit of 3.9GB.

/* Block header placed immediately before each allocated chunk */
typedef struct _BLOCK_HEADER {
    size_t size;               /* total size of this block (including header) */
    struct _BLOCK_HEADER* next; /* next free block in the free list */
} BLOCK_HEADER;

/* Initialize the free list to cover the whole heap */
void init_heap(void);

/* Simple Memset */
void* kmemset(void* dest, int val, uint32_t len);

/* Added Memcpy */
void* kmemcpy(void* dest, const void* src, uint32_t len);

/* Simple allocator/free */
void* kmalloc(size_t size, size_t align);
void kfree(void* ptr);

/* For debugging, merge adjacent free blocks */
void coalesce_free_list(void);

#endif /* MEMORY_H */
