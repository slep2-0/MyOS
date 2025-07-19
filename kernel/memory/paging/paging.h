/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     Paging and Dynamic Memory Allocation setup header.
 */
#ifndef X86_PAGING_H
#define X86_PAGING_H
#include "../../kernel.h"

// Total physical memory (upto 3.9GB)
#define PHYS_MEM_BASE  ((uintptr_t)&kernel_start)  // 0x10000 from linker
#define PHYS_MEM_SIZE (128 * 1024 * 1024)  // 128 MiB

// Page directory has 1024 entries -> each page-table covers 4 MiB
#define PAGE_DIR_ENTRIES 1024
#define PAGE_TABLE_ENTRIES 1024

// Flags for PDE/PTE
typedef enum _FLAGS {
	PAGE_PRESENT = 1 << 0, // 0 - ANY access to this page will fault, unmapped. 1 - the page is valid and the MMU will translate and allow accesses (subject to RW and USER)
	PAGE_RW = 1 << 1, // 0 - page is read only, 1- page is read & write
	PAGE_USER = 1 << 2, // 0 - supervisor only (kernel), user mode access will cause a fault. 1 - any access (both user mode and kernel mode) is allowed. (IF THIS IS SET, USER-MOED IS ALLOWED)
} FLAGS;

// Symobls that come from the linker script that define the start of page directory and page tables (allocated by linker)
extern uint32_t __pd_start;
extern uint32_t __pt_start;

void paging_init(void);
void set_page_writable(void* virtualaddress, bool writable);
void set_page_user_access(void* virtualaddress, bool user_accessible);
void map_page(void* virtualaddress, void* physicaladdress, uint32_t flags);
bool unmap_page(void* virtualaddress);

#endif