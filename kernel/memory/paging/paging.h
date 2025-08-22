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

// Total physical memory (upto 3.9GB)
#define PHYS_MEM_BASE  ((uintptr_t)&kernel_start)  // 0x10000 from linker
#define PHYS_MEM_SIZE (128 * 1024 * 1024)  // 128 MiB

// Page directory has 1024 entries -> each page-table covers 4 MiB
#define PAGE_DIR_ENTRIES 1024
#define PAGE_TABLE_ENTRIES 1024

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

// Symobls that come from the linker script that define the start of page directory and page tables (allocated by linker)
extern uint64_t __pd_start;
extern uint64_t __pt_start;
extern uint64_t __pt_end;

void paging_init(void);
void set_page_writable(void* virtualaddress, bool writable);
void set_page_user_access(void* virtualaddress, bool user_accessible);
// identity only.
void map_page_identity(void* virtualaddress, void* physicaladdress, uint64_t flags);

void map_page(void* virtualaddress, uintptr_t physicaladdress, uint64_t flags);

void map_range_identity(uint64_t start, uint64_t end, uint64_t flags);
bool unmap_page(void* virtualaddress);

static inline uintptr_t MtTranslateKernelVirtualToPhysical(void* virtualaddr) {
    uintptr_t va = (uintptr_t)virtualaddr;
    if (va < KERNEL_VA_START) {
        // Not a kernel virtual address
        // Could assert, bugcheck, or just return 0
        return 0;
    }
    return va - KERNEL_VA_START + KERNEL_PHYS_BASE;
}

static inline void* MtTranslateKernelPhysicalToVirtual(uintptr_t physaddr) {
    if (physaddr < KERNEL_PHYS_BASE) {
        // Not a kernel physical address
        return NULL;
    }
    return (void*)(physaddr - KERNEL_PHYS_BASE + KERNEL_VA_START);
}

// Enable paging with given PML4 physical address -- found in ASM.
void enable_paging(uint64_t pml4_phys);

#endif