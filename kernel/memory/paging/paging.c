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
    tracelast_func("allocate_page_table");
    // CHECK IRQL.
    enforce_max_irql(PASSIVE_LEVEL);
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
    tracelast_func("map_range_identity");
    enforce_max_irql(PASSIVE_LEVEL);
    for (uint64_t addr = start; addr < end; addr += PAGE_SIZE_4K) {
        map_page((void*)addr, (void*)addr, flags);
    }
}


//
//
// dev notes:
// The reason paging didn't work when switching from 32bit to 64bit.
// Is the UEFI Bootloader - It already setup it's own page tabels, GDT, segments, everything.
// So every memory I accessed after switching to my paging, was INVALID (particularly the framebuffer memory)
// Also it let me to notice that I didn't even cover my whole kernel page table and binary size (mark them as reserved - map_range_identity)
// Solution to both:
// Create a local struct of the boot info that UEFI passes (it passes pointers to the UEFI memory, I just replicated the data there to the kernel local .data section)
// And the solution to the second was to use the ADDRESS of the label the linker passes to the C files - I used the variable itself (probably the instruction that was at the address .text) and not it's address.
// Figured the first one out by looking at the logs of QEMU, didn't understand 1 bit of it at first, but then my mind understood that the faulty address (it was a page fault) was a UEFI address, and it was touched right after
// setting paging (which was my GOP printing function), and also after reversing my kernel in ghidra (specifically paging_init), I saw it used the variable itself, and not it's address. (used kernel_start, not &kernel_start)
//
//

extern GOP_PARAMS gop_local;




void paging_init(void) {
    tracelast_func("paging_init");
    enforce_max_irql(PASSIVE_LEVEL);
    // zero your PML4…
    kmemset(pml4, 0, PAGE_SIZE_4K);
    // carve out the first few tables (PML4→PDPT→PD→PT)
    uint64_t* pdpt = allocate_page_table();
    uint64_t* pd = allocate_page_table();
    uint64_t* pt = allocate_page_table();
    pml4[0] = (uint64_t)pdpt | PAGE_PRESENT | PAGE_RW;
    pdpt[0] = (uint64_t)pd | PAGE_PRESENT | PAGE_RW;
    pd[0] = (uint64_t)pt | PAGE_PRESENT | PAGE_RW;

    const uint64_t flags = PAGE_PRESENT | PAGE_RW;

    map_range_identity((uintptr_t)&__pml4_start,
        (uintptr_t)&__pml4_start + PAGE_SIZE_4K,
        flags);

    map_range_identity((uintptr_t)&__pt_start,
        (uintptr_t)&__pt_end,
        flags);

    map_range_identity((uintptr_t)&kernel_start,
        (uintptr_t)&kernel_end + 1,
        flags);

    extern uint8_t __stack_start[], __stack_end[];
    map_range_identity((uintptr_t)__stack_start,
        (uintptr_t)__stack_end,
        flags);

    {
        uint64_t fb = gop_local.FrameBufferBase;
        uint64_t end = fb + gop_local.FrameBufferSize;
        map_range_identity(fb, end, flags);
    }

    extern IDT_ENTRY64 IDT[];
    map_range_identity((uintptr_t)IDT,
        (uintptr_t)IDT + sizeof(IDT_ENTRY64) * IDT_ENTRIES,
        flags);

    // …then load CR3 and enable paging as before…
    enable_paging((uint64_t)pml4);
}


// Map a 4KiB page: map virtual address to physical with given flags (PAGE_RW, PAGE_USER, etc)
void map_page(void* virtualaddress, void* physicaladdress, uint64_t flags) {
    tracelast_func("map_page");
    enforce_max_irql(PASSIVE_LEVEL);
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
    // only flush if paging is already on:
    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    if (cr0 & 0x80000000) {
        invlpg(va);
    }
}

// Unmap a page (remove mapping and free frame)
bool unmap_page(void* virtualaddress) {
    tracelast_func("unmap_page");
    enforce_max_irql(PASSIVE_LEVEL);
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

    // only flush if paging is already on:
    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    if (cr0 & 0x80000000) {
        invlpg(va);
    }

    // Free physical frame if applicable
    free_frame(phys_addr);

    return true;
}

// Set writable flag on a page
void set_page_writable(void* virtualaddress, bool writable) {
    tracelast_func("set_page_writable");
    enforce_max_irql(PASSIVE_LEVEL);
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

    // only flush if paging is already on:
    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    if (cr0 & 0x80000000) {
        invlpg(va);
    }
}

void set_page_user_access(void* virtualaddress, bool user_accessible) {
    tracelast_func("set_page_user_access");
    enforce_max_irql(PASSIVE_LEVEL);
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

    // only flush if paging is already on:
    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    if (cr0 & 0x80000000) {
        invlpg(va);
    }
}
