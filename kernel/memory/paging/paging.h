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

// Flags for PDE/PTE
typedef enum _FLAGS {
	PAGE_PRESENT = 1 << 0, // 0 - ANY access to this page will fault, unmapped. 1 - the page is valid and the MMU will translate and allow accesses (subject to RW and USER)
	PAGE_RW = 1 << 1, // 0 - page is read only, 1- page is read & write
	PAGE_USER = 1 << 2, // 0 - supervisor only (kernel), user mode access will cause a fault. 1 - any access (both user mode and kernel mode) is allowed. (IF THIS IS SET, USER-MOED IS ALLOWED)
    PAGE_PWT       =     0x8,
    PAGE_PCD       =     0x10,
    PAGE_ACCESSED  =     0x20,
    PAGE_DIRTY     =     0x40,
    PAGE_PS        =     0x80,
    PAGE_GLOBAL    =     0x100,
} FLAGS;

void set_page_writable(void* virtualaddress, bool writable);
void set_page_user_access(void* virtualaddress, bool user_accessible);
void map_page(void* virtualaddress, uintptr_t physicaladdress, uint64_t flags);
bool unmap_page(void* virtualaddress);

bool MtIsAddressValid(void* virtualAddr);

/// <summary>
/// UNUSED
/// </summary>
/// <param name="virtualaddr"></param>
/// <returns></returns>
static inline uintptr_t MtTranslateKernelVirtualToPhysical(void* virtualaddr) {
    uintptr_t va = (uintptr_t)virtualaddr;
    if (va < KERNEL_VA_START) {
        // Not a kernel virtual address
        // Could assert, bugcheck, or just return 0
        return 0;
    }
    return va - KERNEL_VA_START + KERNEL_PHYS_BASE;
}

/// <summary>
/// UNUSED
/// </summary>
/// <param name="physaddr"></param>
/// <returns></returns>
static inline void* MtTranslateKernelPhysicalToVirtual(uintptr_t physaddr) {
    // Prefer high-half mapping
    if (physaddr >= KERNEL_PHYS_BASE) {
        return (void*)(physaddr - KERNEL_PHYS_BASE + KERNEL_VA_START);
    }
    // Fallback: assume bootloader identity-mapped the physical range.
    // This is safe only because you identity-mapped usable memory earlier.
    return (void*)(uintptr_t)physaddr;
}

#endif
