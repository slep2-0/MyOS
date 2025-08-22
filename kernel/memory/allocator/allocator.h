/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:		Dynamic Memory Allocation Header.
 */
#ifndef X86_ALLOCATOR_H
#define X86_ALLOCATOR_H

// Standard headers, required.
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "../../cpu/cpu.h"
#include "../../trace.h"
#include "uefi_memory.h"

 // Total physical memory (up to 3.9GB)
#ifndef PHYS_MEM_SIZE
#define PHYS_MEM_SIZE (128 * 1024 * 1024)  // 128 MiB
#endif

#define FRAME_SIZE 4096U
#define MAX_FRAMES (PHYS_MEM_SIZE / FRAME_SIZE)

/// <summary>
///  UNUSED!.
/// </summary>
typedef enum _MEMORY_DESCRIPTOR {
	Free = EfiConventionalMemory,
	TempFree,
	Bad,
} MEMORY_DESCRIPTOR;

/// <summary>
/// A function to return a boolean value if the type of UEFI Memory is usable for memory allocation.
/// </summary>
/// <param name="type">EfiMemoryType</param>
/// <returns>True or False based on the "type" param.</returns>
static inline bool classify(int type) {
	if (type == EfiConventionalMemory) {
		return true;
	}
	if (type == EfiBootServicesCode || type == EfiBootServicesData) {
		return true;
	}
	return false;
}

// Initialize frame bitmap based on UEFI memory map
// Must be called after gEfiMemoryMap* variables are set
void frame_bitmap_init(void);

// Allocate one 4KiB physical frame; returns physical address or 0 if NULL.
uintptr_t alloc_frame(void);

// Free a previously allocated frame (pass the physical address)
void free_frame(uintptr_t p);

#endif // ALLOCATOR_H
