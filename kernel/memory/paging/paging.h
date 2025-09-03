/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     Paging and Dynamic Memory Allocation setup header.
 */
#ifndef X86_PAGING_H
#define X86_PAGING_H


// Standard headers, required.
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "../../cpu/cpu.h"
#include "../../drivers/gop/gop.h"
#include "../../trace.h"

#define KERNEL_VA_START 0xfffff80000000000ULL
#define KERNEL_PHYS_BASE 0x100000
#define MEM_TO_MAP 0x100000000ULL
#define UNMAPPED_LOW_MEM_SIZE 0x10000

// Flags for PDE/PTE
// Included = BIT SET (1)
// Not Included = BIT NOT SET (0)
typedef enum _FLAGS {
    PAGE_PRESENT = 1 << 0,  // Bit 0
    // 0 = page not present (access causes page fault)
    // 1 = page is present, MMU translates virtual addresses

    PAGE_RW = 1 << 1,  // Bit 1
    // 0 = read-only
    // 1 = read/write

    PAGE_USER = 1 << 2,  // Bit 2
    // 0 = supervisor (kernel) only
    // 1 = user-mode access allowed

    PAGE_PWT = 0x8,     // Bit 3
    // Page Write-Through
    // 0 = write-back caching
    // 1 = write-through caching

    PAGE_PCD = 0x10,    // Bit 4
    // Page Cache Disable
    // 0 = cacheable
    // 1 = cache disabled

    PAGE_ACCESSED = 0x20,    // Bit 5
    // Set by CPU when page is read or written

    PAGE_DIRTY = 0x40,    // Bit 6
    // Set by CPU when page is written to

    PAGE_PS = 0x80,    // Bit 7
    // Page Size
    // 0 = normal 4KB page
    // 1 = large page (4MB in PDE, 2MB in PTE for PAE/long mode)

    PAGE_GLOBAL = 0x100,   // Bit 8
    // Global page
    // Not flushed from TLB on CR3 reload
} FLAGS;

void set_page_writable(void* virtualaddress, bool writable);
void set_page_user_access(void* virtualaddress, bool user_accessible);
void map_page(void* virtualaddress, uintptr_t physicaladdress, uint64_t flags);
bool unmap_page(void* virtualaddress);

/// <summary>
/// This function checks if the virtual address given to it, is valid and present in the page tables of the kernel.
/// </summary>
/// <param name="virtualAddr"></param>
/// <returns>True or False based if present or not.</returns>
bool MtIsAddressValid(void* virtualAddr);

/// <summary>
/// This function translates the virtual address to its corresponding physical address in the page tables if present.
/// </summary>
/// <param name="virtualaddress"></param>
/// <returns>Physical Address or 0 if not found.</returns>
uintptr_t MtTranslateVirtualToPhysical(void* virtualaddress);

/// <summary>
/// This function adds (doesn't set) flags to the specified virtual address (if exists).
/// This DOES NOT set flags! (Which means flags that are ON will stay ON and will not get rewritten)
/// </summary>
/// <param name="virtualaddress">VA</param>
/// <param name="flags">PAGE_FLAGS flags.</param>
void MtAddPageFlags(void* virtualaddress, uint64_t flags);

static inline void* MtTranslateKernelPhysicalToVirtual(uintptr_t phys) {
    // assume PHYS_MEM_OFFSET and MEM_TO_MAP/UNMAPPED_LOW_MEM_SIZE are known
    if (phys >= UNMAPPED_LOW_MEM_SIZE && phys < MEM_TO_MAP) {
        return (void*)(phys + PHYS_MEM_OFFSET);
    }
    return NULL; // not mapped
}

static inline uintptr_t MtTranslateKernelVirtualToPhysical(void* v) {
    uintptr_t va = (uintptr_t)v;
    if (va >= PHYS_MEM_OFFSET && va < PHYS_MEM_OFFSET + MEM_TO_MAP) {
        return va - PHYS_MEM_OFFSET;
    }
    return 0; // not in direct-map window
}

#endif
