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

#define HEADER_MAGIC 0x4D544842 // "MTHB"
#define FOOTER_MAGIC 0x4D544642 // "MTFB"

/* Zero Out BSS */
extern uint8_t bss_start;
extern uint8_t bss_end;

void zero_bss(void);

/* Heap Size */

/* Start and end of the heap region */
#define HEAP_START ((uintptr_t)(&kernel_end))
#define HEAP_END   (PHYS_MEM_BASE + PHYS_MEM_SIZE)

#define HEAP_SIZE  (HEAP_END - HEAP_START)

typedef struct _BLOCK_FOOTER {
    uint32_t magic;
} BLOCK_FOOTER;


/* Block header placed immediately before each allocated chunk */
typedef struct _BLOCK_HEADER {
    uint32_t magic;
    size_t block_size;              /* Total size of this block (header + padding + data + footer) */
    size_t requested_size;          /* The original size the user requested, used to find the footer */
    struct _BLOCK_HEADER* next;     /* Next free block in the free list */
    bool in_use;
    uint32_t kind;
} BLOCK_HEADER;

enum { BLK_NORMAL = 0, BLK_EX = 1, BLK_GUARDED = 2, };

/* Initialize the free list to cover the whole heap */
void init_heap(void);

/* Simple Memset */
void* kmemset(void* dest, int64_t val, uint64_t len);

/* Added Memcpy */
void* kmemcpy(void* dest, const void* src, size_t len);

/// <summary>
/// Allocates a block of memory from the kernel’s memory manager.
/// </summary>
/// <param name="size">Size in bytes to allocate</param>
/// <param name="align">Alignment for each byte block (use internal structs for process \ other - use _Alignof)</param>
/// <returns>Pointer to start of allocated memory</returns>
void* MtAllocateVirtualMemory(size_t size, size_t align);

/// <summary>
/// Allocates a region of virtual memory with a specific size and alignment,
/// placing an unmapped guard page immediately after the allocation to catch
/// buffer overflows.
/// </summary>
/// <remarks>
/// This allocator is page-based and should be used for larger allocations
/// where overflow detection is critical (e.g., stacks, large buffers).
/// The allocated region will be at least one page in size.
/// </remarks>
/// <param name="wanted_size">The size of the memory to allocate.</param>
/// <param name="align">The required alignment, must be a power of two.</param>
/// <returns>A pointer to the beginning of the allocated and aligned memory block.</returns>
void* MtAllocateGuardedVirtualMemory(size_t wanted_size, size_t align);

/// <summary>
/// **THE USE OF THIS FUNCTION IS NOT RECOMMENDED -- TO ADD FLAGS TO AN ALLOCATED MEMORY BUFFER USE MtAddPageFlags TO ITS POINTER!!!**
/// ** FREEING THE MEMORY OF THIS BUFFER WILL RESULT IN A PAGE FAULT **
/// Allocates a block of memory from the kernel's memory manager, and sets the paging flags according to the user.
/// </summary>
/// <param name="size">Size in bytes to allocate</param>
/// <param name="align">Alignment for each byteblock (use internal structs for process \ other - use _Alignof)</param>
/// <param name="flags">PAGE_FLAGS flags.</param>
/// <returns>Pointer to start of allocated memory</returns>
void* MtAllocateVirtualMemoryEx(size_t size, size_t align, uint64_t flags);

/// <summary>
/// Releases (frees) a previously allocated block of memory back to the kernel’s memory manager. (DOES NOT UNMAP IF FUNCTION USED IS MtAllocateVirtualMemory !)
/// </summary>
/// <param name="ptr">Pointer to the allocated memory block to free</param>
void MtFreeVirtualMemory(void* ptr);

/// <summary>
/// Returns if the heap pointer given has been allocated by MtAllocateVirtualMemory or not.
/// </summary>
/// <param name="ptr">Address of what is given from MtAllocateVirtualMemory.</param>
/// <returns></returns>
bool MtIsHeapAddressAllocated(void* ptr);

#endif /* MEMORY_H */
