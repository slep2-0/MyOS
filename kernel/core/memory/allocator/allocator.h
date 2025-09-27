/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:		Dynamic Memory Allocation Header.
 */
#ifndef X86_ALLOCATOR_H
#define X86_ALLOCATOR_H

/// This offset marks the offset between physical memory and its kernel virtual equivalent, used for mapping physical (not identically).
#define PHYS_MEM_OFFSET 0xffff880000000000ULL

// Standard headers, required.
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "../../../cpu/cpu.h"
#include "../../../trace.h"
#include "../../uefi_memory.h"

#define FRAME_SIZE 4096ULL

/// <summary>
/// A function to return a boolean value if the type of UEFI Memory is usable for memory allocation.
/// </summary>
/// <param name="type">EfiMemoryType</param>
/// <returns>True or False based on the type of Efi Memory passed.</returns>
static inline bool classify(int type) {
	return type == EfiConventionalMemory;
}

// Initialize frame bitmap based on UEFI memory map
// Must be called after gEfiMemoryMap* variables are set
void frame_bitmap_init(void);

// Allocate one 4KiB physical frame; returns physical address or 0 if NULL.
uintptr_t alloc_frame(void);

// Free a previously allocated frame (pass the physical address)
void free_frame(uintptr_t p);

#endif // ALLOCATOR_H
