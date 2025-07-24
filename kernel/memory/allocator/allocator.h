/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:		Dynamic Memory Allocation Header.
 */
#ifndef X86_ALLOCATOR_H
#define X86_ALLOCATOR_H

#include "../../kernel.h"

 // Total physical memory (up to 3.9GB)
#ifndef PHYS_MEM_SIZE
#define PHYS_MEM_SIZE (128 * 1024 * 1024)  // 128 MiB
#endif

#define FRAME_SIZE 4096U
#define MAX_FRAMES (PHYS_MEM_SIZE / FRAME_SIZE)

// Initialize frame bitmap based on UEFI memory map
// Must be called after gEfiMemoryMap* variables are set
void frame_bitmap_init(void);

// Allocate one 4KiB physical frame; returns physical address or NULL
void* alloc_frame(void);

// Free a previously allocated frame (pass the physical address)
void free_frame(void* p);

#endif // ALLOCATOR_H
