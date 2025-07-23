/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     64-bit Memory Paging Implementation (4-level paging)
 */

#include "paging.h"

 // Constants for x86_64 paging
#define PAGE_ENTRIES        512
#define PAGE_SIZE_4K        0x1000

// Page flags
#define PAGE_PRESENT        0x1
#define PAGE_RW             0x2
#define PAGE_USER           0x4
#define PAGE_PWT            0x8
#define PAGE_PCD            0x10
#define PAGE_ACCESSED       0x20
#define PAGE_DIRTY          0x40
#define PAGE_PS             0x80
#define PAGE_GLOBAL         0x100

// Extern symbols for bootloader-allocated pages, or allocate dynamically in your kernel
extern uint64_t __pml4_start[]; // Allocate this page aligned to 4KiB

static uint64_t* pml4 = __pml4_start; // Base of PML4 table

// Helper: allocate a zeroed 4KiB-aligned page for new tables
static uint64_t* allocate_page_table() {
    // Implement your own allocator here
    // For now, you can use kmalloc aligned to PAGE_SIZE_4K or a static buffer
    uint64_t* page = (uint64_t*)kmalloc(PAGE_SIZE_4K, PAGE_SIZE_4K);
    if (page) {
        kmemset(page, 0, PAGE_SIZE_4K);
    }
    return page;
}

// Extract indices from virtual address
static inline size_t get_pml4_index(uint64_t va) { return (va >> 39) & 0x1FF; }
static inline size_t get_pdpt_index(uint64_t va) { return (va >> 30) & 0x1FF; }
static inline size_t get_pd_index(uint64_t va) { return (va >> 21) & 0x1FF; }
static inline size_t get_pt_index(uint64_t va) { return (va >> 12) & 0x1FF; }

// Initialize paging (map identity for first ~4 GiB for now)
void paging_init(void) {
    // Zero PML4
    kmemset(pml4, 0, PAGE_SIZE_4K);

    // Allocate PDPT, PD, PT tables
    uint64_t* pdpt = allocate_page_table();
    uint64_t* pd = allocate_page_table();
    uint64_t* pt = allocate_page_table();

    if (!pdpt || !pd || !pt) {
        // Handle allocation failure (panic, halt)
        return;
    }

    // Map the first 4 MiB pages with identity mapping (for kernel)
    // Build the hierarchy:
    // pml4[0] -> pdpt
    pml4[0] = ((uint64_t)pdpt) | PAGE_PRESENT | PAGE_RW;

    // pdpt[0] -> pd
    pdpt[0] = ((uint64_t)pd) | PAGE_PRESENT | PAGE_RW;

    // pd[0] -> pt
    pd[0] = ((uint64_t)pt) | PAGE_PRESENT | PAGE_RW;

    // Fill PT with identity mapping for first 2 MiB (512 * 4KiB = 2 MiB)
    for (size_t i = 0; i < PAGE_ENTRIES; i++) {
        pt[i] = (i * PAGE_SIZE_4K) | PAGE_PRESENT | PAGE_RW;
    }

    // Load CR3 with PML4 physical address
    uint64_t pml4_phys = (uint64_t)pml4; // Must be physical address in your kernel setup
    __asm__ volatile (
        "mov %0, %%cr3\n"
        "mov %%cr0, %%rax\n"
        "or $0x80000000, %%eax\n"    // Enable paging (PG bit)
        "or $0x00010000, %%eax\n"    // Enable WP bit
        "mov %%rax, %%cr0\n"
        :
    : "r"(pml4_phys)
        : "rax"
        );
}

// Map a 4KiB page: map virtual address to physical with given flags (PAGE_RW, PAGE_USER, etc)
void map_page(void* virtualaddress, void* physicaladdress, uint64_t flags) {
    uint64_t va = (uint64_t)virtualaddress;
    uint64_t pa = (uint64_t)physicaladdress;

    size_t pml4_i = get_pml4_index(va);
    size_t pdpt_i = get_pdpt_index(va);
    size_t pd_i = get_pd_index(va);
    size_t pt_i = get_pt_index(va);

    // Traverse or allocate PDPT
    uint64_t* pdpt;
    if (!(pml4[pml4_i] & PAGE_PRESENT)) {
        pdpt = allocate_page_table();
        if (!pdpt) return; // fail
        pml4[pml4_i] = ((uint64_t)pdpt) | PAGE_PRESENT | PAGE_RW | PAGE_USER;
    }
    else {
        pdpt = (uint64_t*)(pml4[pml4_i] & ~0xFFFULL);
    }

    // Traverse or allocate PD
    uint64_t* pd;
    if (!(pdpt[pdpt_i] & PAGE_PRESENT)) {
        pd = allocate_page_table();
        if (!pd) return; // fail
        pdpt[pdpt_i] = ((uint64_t)pd) | PAGE_PRESENT | PAGE_RW | PAGE_USER;
    }
    else {
        pd = (uint64_t*)(pdpt[pdpt_i] & ~0xFFFULL);
    }

    // Traverse or allocate PT
    uint64_t* pt;
    if (!(pd[pd_i] & PAGE_PRESENT)) {
        pt = allocate_page_table();
        if (!pt) return; // fail
        pd[pd_i] = ((uint64_t)pt) | PAGE_PRESENT | PAGE_RW | PAGE_USER;
    }
    else {
        pt = (uint64_t*)(pd[pd_i] & ~0xFFFULL);
    }

    // Map the page
    pt[pt_i] = (pa & ~0xFFFULL) | (flags & (PAGE_RW | PAGE_USER)) | PAGE_PRESENT;

    // Flush TLB for this virtual address
    invlpg(virtualaddress);
}

// Unmap a page (remove mapping and free frame)
bool unmap_page(void* virtualaddress) {
    uint64_t va = (uint64_t)virtualaddress;

    size_t pml4_i = get_pml4_index(va);
    size_t pdpt_i = get_pdpt_index(va);
    size_t pd_i = get_pd_index(va);
    size_t pt_i = get_pt_index(va);

    if (!(pml4[pml4_i] & PAGE_PRESENT)) return false;
    uint64_t* pdpt = (uint64_t*)(pml4[pml4_i] & ~0xFFFULL);

    if (!(pdpt[pdpt_i] & PAGE_PRESENT)) return false;
    uint64_t* pd = (uint64_t*)(pdpt[pdpt_i] & ~0xFFFULL);

    if (!(pd[pd_i] & PAGE_PRESENT)) return false;
    uint64_t* pt = (uint64_t*)(pd[pd_i] & ~0xFFFULL);

    if (!(pt[pt_i] & PAGE_PRESENT)) return false;

    void* phys_addr = (void*)(pt[pt_i] & ~0xFFFULL);

    // Clear the entry
    pt[pt_i] = 0;

    invlpg(virtualaddress);

    // Free physical frame if applicable
    free_frame(phys_addr);

    return true;
}

// Set writable flag on a page
void set_page_writable(void* virtualaddress, bool writable) {
    uint64_t va = (uint64_t)virtualaddress;

    size_t pml4_i = get_pml4_index(va);
    size_t pdpt_i = get_pdpt_index(va);
    size_t pd_i = get_pd_index(va);
    size_t pt_i = get_pt_index(va);

    if (!(pml4[pml4_i] & PAGE_PRESENT)) return;
    uint64_t* pdpt = (uint64_t*)(pml4[pml4_i] & ~0xFFFULL);

    if (!(pdpt[pdpt_i] & PAGE_PRESENT)) return;
    uint64_t* pd = (uint64_t*)(pdpt[pdpt_i] & ~0xFFFULL);

    if (!(pd[pd_i] & PAGE_PRESENT)) return;
    uint64_t* pt = (uint64_t*)(pd[pd_i] & ~0xFFFULL);

    uint64_t entry = pt[pt_i];
    if (writable) {
        entry |= PAGE_RW;
    }
    else {
        entry &= ~((uint64_t)PAGE_RW);
    }
    pt[pt_i] = entry;

    invlpg(virtualaddress);
}

void set_page_user_access(void* virtualaddress, bool user_accessible) {
    uint64_t va = (uint64_t)virtualaddress;

    size_t pml4_i = get_pml4_index(va);
    size_t pdpt_i = get_pdpt_index(va);
    size_t pd_i = get_pd_index(va);
    size_t pt_i = get_pt_index(va);

    // Assume these pointers are declared and initialized elsewhere:
    extern uint64_t* pml4;

    if (!(pml4[pml4_i] & PAGE_PRESENT)) return;
    uint64_t* pdpt = (uint64_t*)((pml4[pml4_i] & ~0xFFFULL));
    if (!(pdpt[pdpt_i] & PAGE_PRESENT)) return;
    uint64_t* pd = (uint64_t*)((pdpt[pdpt_i] & ~0xFFFULL));
    if (!(pd[pd_i] & PAGE_PRESENT)) return;
    uint64_t* pt = (uint64_t*)((pd[pd_i] & ~0xFFFULL));
    if (!(pt[pt_i] & PAGE_PRESENT)) return;

    uint64_t entry = pt[pt_i];
    if (user_accessible) {
        entry |= PAGE_USER;
    }
    else {
        entry &= ~((uint64_t)PAGE_USER);
    }
    pt[pt_i] = entry;

    invlpg(virtualaddress);
}
