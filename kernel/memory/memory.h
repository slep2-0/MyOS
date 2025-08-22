/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     Memory Management Header
 */

#ifndef MEMORY_H
#define MEMORY_H
 // Standard headers, required.
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "../cpu/cpu.h"
#include "../memory/allocator/allocator.h"
#include "../memory/paging/paging.h"
#include "../trace.h"
 /* Symbol defined in linker script, end of loaded kernel */
extern uint8_t kernel_end;
extern uint8_t kernel_start;
extern const size_t kernel_length;

/* Zero Out BSS */
extern uint8_t bss_start;
extern uint8_t bss_end;

void zero_bss(void);

/* Heap Size */

/* Start and end of the heap region */
#define HEAP_START_PHYS ((uintptr_t)((((uintptr_t)&kernel_end + 0xFFFU) & ~0xFFFU)))
#define HEAP_END_PHYS   (PHYS_MEM_BASE + PHYS_MEM_SIZE)

#define HEAP_START_VA ((uintptr_t)MtTranslateKernelPhysicalToVirtual(HEAP_START_PHYS))
#define HEAP_END_VA   ((uintptr_t)MtTranslateKernelPhysicalToVirtual(HEAP_END_PHYS))

#define HEAP_SIZE_VA  (HEAP_END_VA - HEAP_START_VA)

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

/// <summary>
/// Allocates a block of memory from the kernel’s memory manager.
/// </summary>
/// <param name="size">Size in bytes to allocate</param>
/// <param name="align">Alignment for each byte block (use internal structs for process \ other - use _Alignof)</param>
/// <returns>Pointer to start of allocated memory</returns>
void* MtAllocateMemory(size_t size, size_t align);

/// <summary>
/// Releases (frees) a previously allocated block of memory back to the kernel’s memory manager.
/// </summary>
/// <param name="ptr">Pointer to the allocated memory block to free</param>
void MtFreeMemory(void* ptr);

#endif /* MEMORY_H */
