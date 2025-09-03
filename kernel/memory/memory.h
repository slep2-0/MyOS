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
bool check_bss_zeroed(void);

/* Heap Size */

/* Start and end of the heap region */
#define HEAP_START ((uintptr_t)(&kernel_end))
#define HEAP_END   (PHYS_MEM_BASE + PHYS_MEM_SIZE)

#define HEAP_SIZE  (HEAP_END - HEAP_START)

/* Block header placed immediately before each allocated chunk */
typedef struct _BLOCK_HEADER {
    size_t size;               /* total size of this block (including header) */
    struct _BLOCK_HEADER* next; /* next free block in the free list */
    bool in_use;
    uint32_t kind;
} BLOCK_HEADER;

enum { BLK_NORMAL = 0, BLK_EX = 1 };

/* Initialize the free list to cover the whole heap */
void init_heap(void);

/* Simple Memset */
void* kmemset(void* dest, int64_t val, uint64_t len);

/* Added Memcpy */
void* kmemcpy(void* dest, const void* src, uint32_t len);

/// <summary>
/// Allocates a block of memory from the kernel’s memory manager.
/// </summary>
/// <param name="size">Size in bytes to allocate</param>
/// <param name="align">Alignment for each byte block (use internal structs for process \ other - use _Alignof) (**ALIGN MUST BE POWER OF 2 OR NON ZERO**)</param>
/// <returns>Pointer to start of allocated memory</returns>
void* MtAllocateVirtualMemory(size_t size, size_t align);

/// <summary>
/// **THE USE OF THIS FUNCTION IS NOT RECOMMENDED -- TO ADD FLAGS TO AN ALLOCATED MEMORY BUFFER USE MtAddPageFlags TO ITS POINTER!!!**
/// ** FREEING THE MEMORY OF THIS BUFFER WILL RESULT IN A PAGE FAULT **
/// Allocates a block of memory from the kernel's memory manager, and sets the paging flags according to the user.
/// </summary>
/// <param name="size">Size in bytes to allocate</param>
/// <param name="align">Alignment for each byteblock (use internal structs for process \ other - use _Alignof) (**ALIGN MUST BE POWER OF 2 OR NON ZERO**)</param>
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
