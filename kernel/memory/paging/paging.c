/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     64-bit Memory Paging Implementation (4-level paging)
 */

#include "paging.h"

 // Constants for x86_64 paging
#define PAGE_ENTRIES        512
#define PAGE_SIZE_4K        0x1000

// Extern symbols for bootloader-allocated pages, or allocate dynamically in your kernel
extern uint64_t __pml4_start[]; // Allocate this page aligned to 4KiB

static uint64_t* pml4 = __pml4_start; // Base of PML4 table

static uint8_t* next_pt = (uint8_t*)&__pt_start;
static uint8_t* const end_pt = (uint8_t*)&__pt_end;

static uint64_t* allocate_page_table(void) {
    // 1) if we still have one of the linker‑reserved tables, carve that out:
    if (next_pt + PAGE_SIZE_4K <= end_pt) {
        uint64_t* table = (uint64_t*)next_pt;
        next_pt += PAGE_SIZE_4K;
        kmemset(table, 0, PAGE_SIZE_4K);
        return table;
    }

    // 2) otherwise fall back on the frame‐bitmap
    void* phys = alloc_frame();
    if (!phys) {
        bugcheck_system(NULL, BAD_PAGING, 0, true);
        return NULL;
    }
    // zero it before use
    kmemset(phys, 0, PAGE_SIZE_4K);
    return (uint64_t*)phys;
}

// Extract indices from virtual address
static inline size_t get_pml4_index(uint64_t va) { return (va >> 39) & 0x1FF; }
static inline size_t get_pdpt_index(uint64_t va) { return (va >> 30) & 0x1FF; }
static inline size_t get_pd_index(uint64_t va) { return (va >> 21) & 0x1FF; }
static inline size_t get_pt_index(uint64_t va) { return (va >> 12) & 0x1FF; }

void map_range_identity(uint64_t start, uint64_t end, uint64_t flags) {
    for (uint64_t addr = start; addr < end; addr += PAGE_SIZE_4K) {
        map_page((void*)addr, (void*)addr, flags);
    }
}

// Initialize paging (map identity for first ~4 GiB for now)
void paging_init(void) {
    // zero PML4
    kmemset(pml4, 0, PAGE_SIZE_4K);

    // carve out the first few tables
    uint64_t* pdpt = allocate_page_table();
    uint64_t* pd = allocate_page_table();
    uint64_t* pt = allocate_page_table();
    // … error check …

    // hook them up:
    pml4[0] = (uint64_t)pdpt | PAGE_PRESENT | PAGE_RW;
    pdpt[0] = (uint64_t)pd | PAGE_PRESENT | PAGE_RW;
    pd[0] = (uint64_t)pt | PAGE_PRESENT | PAGE_RW;

    // identity‑map only the ranges you actually care about:
    //  · the low 4 MiB kernel + stack
    map_range_identity(0x00000000, 0x00400000, PAGE_PRESENT | PAGE_RW);

    //  · your PML4 page itself
    map_range_identity((uintptr_t)__pml4_start,
        (uintptr_t)__pml4_start + PAGE_SIZE_4K,
        PAGE_PRESENT | PAGE_RW);

    //  · and the kernel text/data
    map_range_identity((uintptr_t)kernel_start,
        (uintptr_t)kernel_end,
        PAGE_PRESENT | PAGE_RW);

    // Map the UEFI stack (example: 0x7E00000–0x7E10000)
    map_range_identity(0x7e00000, 0x7e10000, PAGE_PRESENT | PAGE_RW);

    // Map kernel stack.
    extern uint8_t __stack_start[], __stack_end[];
    map_range_identity((uint64_t)__stack_start,
        (uint64_t)__stack_end,
        PAGE_PRESENT | PAGE_RW);

    // done!  now load CR3
    uint64_t pml4_phys = (uint64_t)pml4;
    __asm__ volatile (
        "mov %0, %%cr3\n"
        "mov %%cr0, %%rax\n"
        "or  $0x80000000, %%eax\n"  // PG
        "or  $0x00010000, %%eax\n"  // WP
        "mov %%rax, %%cr0\n"
        : : "r"(pml4_phys) : "rax"
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
