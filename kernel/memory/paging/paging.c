/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     Memory Paging and Dynamic Memory Allocation Implementation
 */
#include "paging.h"

static uint32_t *page_directory = &__pd_start;
static uint32_t *page_tables = &__pt_start;

void paging_init(void) {
	// zero out page directory and all page tables.
	kmemset(page_directory, 0, 4096);
	// how many page tables we need
	uint32_t num_pt = (PHYS_MEM_SIZE + 0x3FFFFF) / 0x400000;
	kmemset(page_tables, 0, num_pt * 4096);

	// build entries now. - only map as necessary, rest is zero (unmapped).

	for (uint32_t pd_idx = 0; pd_idx < num_pt; pd_idx++) {
		uint32_t* pt = page_tables + (pd_idx * PAGE_TABLE_ENTRIES);
		page_directory[pd_idx] = ((uint32_t)pt) | PAGE_PRESENT | PAGE_RW;

		// fill each page table entry to map 4 KiB*1024 = 4 MiB block.
		for (uint32_t pt_idx = 0; pt_idx < PAGE_TABLE_ENTRIES; pt_idx++) {
			uint32_t phys = (pd_idx << 22) | (pt_idx << 12);
			pt[pt_idx] = phys | PAGE_PRESENT | PAGE_RW;
		}
	}

	// load cr3 and enable paging ( cr0.PG = bit 31)
    __asm__ volatile (
        "mov %0, %%cr3           \n"
        "mov %%cr0, %%eax        \n"
        "or  $0x80000000, %%eax  \n"
        "or  $0x00010000, %%eax  \n" // setting CR0.WP (write protection) to 1, so the CPU will NOT honor the kernel writing to read only memory.
        "mov %%eax, %%cr0        \n"
        : : "r"((uint32_t)page_directory)
        : "eax"
        );
}

// Set the RW bit to 'writable' bool -> true = readwrite, false = readonly
void set_page_writable(void* virtualaddress, bool writable) {
    uint32_t v = (uint32_t)virtualaddress;
    uint32_t pd_idx = v >> 22;
    uint32_t pt_idx = (v >> 12) & 0x3FF;
    uint32_t* pt_base = (uint32_t*)&__pt_start;
    uint32_t* pt = pt_base + pd_idx * PAGE_TABLE_ENTRIES;
    uint32_t entry = pt[pt_idx];
#ifdef DEBUG
    print_to_screen("set_page_writable: VA=0x", COLOR_CYAN);
    print_hex(v, COLOR_CYAN);
    print_to_screen(" PD[", COLOR_CYAN);
    print_hex(pd_idx, COLOR_CYAN);
    print_to_screen("] PT[", COLOR_CYAN);
    print_hex(pt_idx, COLOR_CYAN);
    print_to_screen("]\r\n", COLOR_BLACK);

    print_to_screen("  pt_base=0x", COLOR_CYAN);
    print_hex((uint32_t)pt_base, COLOR_CYAN);
    print_to_screen(" pt=0x", COLOR_CYAN);
    print_hex((uint32_t)pt, COLOR_CYAN);
    print_to_screen("\r\n", COLOR_BLACK);

    print_to_screen("  BEFORE: entry=0x", COLOR_YELLOW);
    print_hex(entry, COLOR_YELLOW);
    print_to_screen("\r\n", COLOR_BLACK);
#endif
    if (writable) {
        entry |= PAGE_RW;
#ifdef DEBUG
        print_to_screen("  Setting WRITABLE\r\n", COLOR_GREEN);
#endif
    }
    else {
        entry &= ~PAGE_RW;
#ifdef DEBUG
        print_to_screen("  Setting READ-ONLY\r\n", COLOR_RED);
#endif
    }

    pt[pt_idx] = entry;
#ifdef DEBUG
    print_to_screen("  AFTER: entry=0x", COLOR_YELLOW);
    print_hex(entry, COLOR_YELLOW);
    print_to_screen("\r\n", COLOR_BLACK);

    // Verify the write actually took effect
    uint32_t verify = pt[pt_idx];
    print_to_screen("  VERIFY: entry=0x", COLOR_YELLOW);
    print_hex(verify, COLOR_YELLOW);
    if (verify == entry) {
        print_to_screen(" (OK)\r\n", COLOR_GREEN);
    }
    else {
        print_to_screen(" (FAILED!)\r\n", COLOR_RED);
    }
#endif
    invlpg(virtualaddress); // flush it from the TLB
#ifdef DEBUG
    print_to_screen("  TLB flushed\r\n", COLOR_CYAN);
#endif
}

// Set the UserSupervisor bit to 'user_accessible' bool -> true = user+kernel, false = kernel only (supervisor)
void set_page_user_access(void* virtualaddress, bool user_accessible) {
	uint32_t v = (uint32_t)virtualaddress;
	uint32_t pd_idx = v >> 22;
	uint32_t pt_idx = (v >> 12) & 0x3FF;
	uint32_t* pt_base = (uint32_t*)&__pt_start;
	uint32_t* pt = pt_base + pd_idx * PAGE_TABLE_ENTRIES;

	uint32_t entry = pt[pt_idx];
	if (user_accessible) {
		entry |= PAGE_USER;
	}
	else {
		entry &= ~PAGE_USER;
	}
	pt[pt_idx] = entry;

	invlpg(virtualaddress);
}

// Map a single 4KB page: Virtual Address -> Physical Address with given flags (PAGE_RW, PAGE_USER, PAGE_PRESENT)
void map_page(void* virtualaddress, void* physicaladdress, uint32_t flags) {
    uint32_t virtualaddr = (uint32_t)virtualaddress;
    uint32_t pagedirectory_i = virtualaddr >> 22;
    uint32_t pagetable_i = (virtualaddr >> 12) & 0x3FF;

    // Locate this page-table (already pre-allocated 32 tables at __pt_start)
    uint32_t* pt_base = (uint32_t*)&__pt_start;
    uint32_t* pt = pt_base + pagedirectory_i * PAGE_TABLE_ENTRIES;

    // Install the entry - physical frame + flags + present bit (PAGE_PRESENT)
    pt[pagetable_i] = ((uint32_t)physicaladdress & ~0xFFF) // frame base
        | (flags & (PAGE_RW | PAGE_USER)) | PAGE_PRESENT;


    // Flush the TLB for the address to apply new changes.
    invlpg(virtualaddress);
}

bool unmap_page(void* virtualaddress) {
    uint32_t va = (uint32_t)virtualaddress;
    uint32_t pd_i = va >> 22;
    uint32_t pt_i = (va >> 12) & 0x3FF;

    uint32_t* page_table_base = (uint32_t*)&__pt_start;
    uint32_t* page_table = page_table_base + pd_i * PAGE_TABLE_ENTRIES;

    if (!(page_directory[pd_i] & PAGE_PRESENT)) {
        // This page table doesn't exist, no page present bit.
        return false;
    }

    uint32_t entry = page_table[pt_i];
    if (!(entry & PAGE_PRESENT)) {
        // Page is already unmapped.
        return true;
    }

    void* phys_addr = (void*)(entry & 0xFFFFF000);

    // Clear the page table entry.
    page_table[pt_i] = 0;

    // Flush TLB To apply changes for this virtual address
    invlpg(virtualaddress);

    // Free the physical frame from memory.
    free_frame(phys_addr);

    return true;
}