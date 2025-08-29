/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     64-bit Memory Paging Implementation (4-level paging)
 * TODO: This code is VERY unorganized, I do not like how it looks, cleanup: merge allocate_page_table, and map_page.
 */

#include "paging.h"
#include "../../interrupts/idt.h"
#include "../memory.h"
#include "../../drivers/ahci/ahci.h"

 // Constants for x86_64 paging
#define PAGE_ENTRIES        512
#define PAGE_SIZE_4K        0x1000

// Constants for aligning paging for AHCI
#define PAGE_MASK        0xFFFULL
#define PAGE_ALIGN_DOWN(x)  ((x) & ~PAGE_MASK)
#define PAGE_ALIGN_UP(x)    (((x) + PAGE_MASK) & ~PAGE_MASK)

#define RECURSIVE_INDEX 0x1FF

// From now on, physical addresses are uintptr_t, and virtual addresses are void pointers.

static inline uint64_t canonical_high(uint64_t addr) {
    // If bit 47 is set, set all higher bits
    if (addr & (1ULL << 47)) {
        return addr | 0xFFFF000000000000ULL;
    }
    return addr;
}


// returns virt = phys
static uint64_t* allocate_page_table_identity(void) {
    tracelast_func("allocate_page_table");
    // CHECK IRQL.
    uint64_t rip;
    GET_RIP(rip);
    enforce_max_irql(DISPATCH_LEVEL, (void*)rip);

    // 2) otherwise fall back on the frame‐bitmap
    void* phys = (void*)alloc_frame();
    if (!phys) {
        CTX_FRAME ctx;
        SAVE_CTX_FRAME(&ctx);
        MtBugcheck(&ctx, NULL, BAD_PAGING, 0, false);
        return NULL; //shouldn't reach here.
    }
    // zero it before use
    kmemset(phys, 0, PAGE_SIZE_4K);
    return (uint64_t*)phys;
}

// Build recursive virtual pointer for different table levels:
// To get PML4 pointer:
static inline uint64_t* pml4_from_recursive(void) {
    uint64_t va = ((uint64_t)RECURSIVE_INDEX << 39) |
        ((uint64_t)RECURSIVE_INDEX << 30) |
        ((uint64_t)RECURSIVE_INDEX << 21) |
        ((uint64_t)RECURSIVE_INDEX << 12);
    va = canonical_high(va);
    return (uint64_t*)(uintptr_t)va;
}

static inline uint64_t* pdpt_from_recursive(size_t pml4_i) {
    uint64_t va = ((uint64_t)RECURSIVE_INDEX << 39) |
        ((uint64_t)RECURSIVE_INDEX << 30) |
        ((uint64_t)RECURSIVE_INDEX << 21) |
        ((uint64_t)pml4_i << 12); // <-- CORRECTED
    va = canonical_high(va);
    return (uint64_t*)(uintptr_t)va;
}

// To get PD page for pml4_i, pdpt_i
static inline uint64_t* pd_from_recursive(size_t pml4_i, size_t pdpt_i) {
    uint64_t va = ((uint64_t)RECURSIVE_INDEX << 39) |
        ((uint64_t)RECURSIVE_INDEX << 30) |
        ((uint64_t)pml4_i << 21) |        // <-- CORRECTED
        ((uint64_t)pdpt_i << 12);       // <-- CORRECTED
    va = canonical_high(va);
    return (uint64_t*)(uintptr_t)va;
}

// To get PT page for pml4_i, pdpt_i, pd_i
static inline uint64_t* pt_from_recursive(size_t pml4_i, size_t pdpt_i, size_t pd_i) {
    uint64_t va = ((uint64_t)RECURSIVE_INDEX << 39) |
        ((uint64_t)pml4_i << 30) |
        ((uint64_t)pdpt_i << 21) |
        ((uint64_t)pd_i << 12);
    va = canonical_high(va);
    return (uint64_t*)(uintptr_t)va;
}

typedef struct _PAGE_FRAME {
    uintptr_t phys;
    void* virt;
} PAGE_FRAME;

static PAGE_FRAME allocate_page_table(void) {
    tracelast_func("allocate_page_table");
    uint64_t rip;
    GET_RIP(rip);
    enforce_max_irql(DISPATCH_LEVEL, (void*)rip);

    uintptr_t phys = alloc_frame();  // must return physical address
    if (!phys) {
        CTX_FRAME ctx;
        SAVE_CTX_FRAME(&ctx);
        MtBugcheck(&ctx, NULL, BAD_PAGING, 0, false);
        PAGE_FRAME null = { 0, NULL };
        return null; /* unreachable if bugcheck halts */
    }

    void* virt = MtTranslateKernelPhysicalToVirtual(phys); // map phys -> virt
    kmemset(virt, 0, PAGE_SIZE_4K);

    PAGE_FRAME retval;
    retval.phys = phys;
    retval.virt = virt;
    return retval;
}

// Extract indices from virtual address
static inline size_t get_pml4_index(uint64_t va) { return (va >> 39) & 0x1FF; }
static inline size_t get_pdpt_index(uint64_t va) { return (va >> 30) & 0x1FF; }
static inline size_t get_pd_index(uint64_t va) { return (va >> 21) & 0x1FF; }
static inline size_t get_pt_index(uint64_t va) { return (va >> 12) & 0x1FF; }

static void map_range_higher(uintptr_t phys_start, uintptr_t phys_end, void* va_start, uint64_t flags) {
    uintptr_t p = phys_start;
    uintptr_t v = (uintptr_t)va_start;

    for (; p < phys_end; p += PAGE_SIZE_4K, v += PAGE_SIZE_4K) {
        map_page((void*)v, p, flags);
    }
}

static void unmap_range_identity(uintptr_t start, uintptr_t end) {
    start = PAGE_ALIGN_DOWN(start);
    end = PAGE_ALIGN_UP(end);

    for (uintptr_t va = start; va < end; va += PAGE_SIZE_4K) {
        unmap_page((void*)va);
    }
}

static uint64_t va_to_phys(void* va) {
    uint64_t v = (uint64_t)va;
    if (v >= KERNEL_VA_START) {
        return MtTranslateKernelVirtualToPhysical((void*)v);
    }
    else {
        return v; // identity mapped addresses equal physical
    }
}


// Paging.c

// Corrected map_page function
void map_page(void* virtualaddress, uintptr_t physicaladdress, uint64_t flags) {
    tracelast_func("map_page");
    uint64_t va = (uint64_t)virtualaddress;
    uintptr_t pa = physicaladdress;

    size_t pml4_i = get_pml4_index(va);
    size_t pdpt_i = get_pdpt_index(va);
    size_t pd_i = get_pd_index(va);
    size_t pt_i = get_pt_index(va);

    uint64_t* pml4_va = pml4_from_recursive();

    // 2. Ensure PDPT exists
    if (!(pml4_va[pml4_i] & PAGE_PRESENT)) {
        uintptr_t new_pdpt_phys = alloc_frame();
        if (!new_pdpt_phys) { /* handle error */ return; }
        kmemset((void*)MtTranslateKernelPhysicalToVirtual(new_pdpt_phys), 0, PAGE_SIZE_4K);
        pml4_va[pml4_i] = new_pdpt_phys | flags;

        // FIX: Invalidate the PDPT's virtual address, not the PT's
        invlpg(pdpt_from_recursive(pml4_i));
    }
    uint64_t* pdpt_va = pdpt_from_recursive(pml4_i);

    // 3. Ensure PD exists
    if (!(pdpt_va[pdpt_i] & PAGE_PRESENT)) {
        uintptr_t new_pd_phys = alloc_frame();
        if (!new_pd_phys) { /* handle error */ return; }
        kmemset((void*)MtTranslateKernelPhysicalToVirtual(new_pd_phys), 0, PAGE_SIZE_4K);
        pdpt_va[pdpt_i] = new_pd_phys | flags;

        // FIX: Invalidate the PD's virtual address
        invlpg(pd_from_recursive(pml4_i, pdpt_i));
    }
    uint64_t* pd_va = pd_from_recursive(pml4_i, pdpt_i);

    // 4. Ensure PT exists
    if (!(pd_va[pd_i] & PAGE_PRESENT)) {
        uintptr_t new_pt_phys = alloc_frame();
        if (!new_pt_phys) { /* handle error */ return; }
        kmemset((void*)MtTranslateKernelPhysicalToVirtual(new_pt_phys), 0, PAGE_SIZE_4K);
        pd_va[pd_i] = new_pt_phys | flags;

        // This one was already correct by coincidence
        invlpg(pt_from_recursive(pml4_i, pdpt_i, pd_i));
    }
    uint64_t* pt_va = pt_from_recursive(pml4_i, pdpt_i, pd_i);

    // 5. Map the page
    pt_va[pt_i] = (pa & ~0xFFFULL) | flags;

    // 6. Flush the TLB for the target virtual address
    invlpg(virtualaddress);
}

// Unmap a page (remove mapping and free frame)
bool unmap_page(void* virtualaddress) {
    tracelast_func("unmap_page");
    uint64_t rip;
    GET_RIP(rip);
    enforce_max_irql(DISPATCH_LEVEL, (void*)rip);
    uint64_t va = (uint64_t)virtualaddress;

    size_t pml4_i = get_pml4_index(va);
    size_t pdpt_i = get_pdpt_index(va);
    size_t pd_i = get_pd_index(va);
    size_t pt_i = get_pt_index(va);

    // FIX: Use recursive helpers to get VIRTUAL addresses of tables
    uint64_t* pml4 = pml4_from_recursive();
    if (!(pml4[pml4_i] & PAGE_PRESENT)) return false;

    uint64_t* pdpt = pdpt_from_recursive(pml4_i);
    if (!(pdpt[pdpt_i] & PAGE_PRESENT)) return false;

    uint64_t* pd = pd_from_recursive(pml4_i, pdpt_i);
    if (!(pd[pd_i] & PAGE_PRESENT)) return false;

    uint64_t* pt = pt_from_recursive(pml4_i, pdpt_i, pd_i);
    if (!(pt[pt_i] & PAGE_PRESENT)) return false;

    uintptr_t phys_addr = (uintptr_t)(pt[pt_i] & ~0xFFFULL);

    pt[pt_i] = 0; // Clear the entry
    invlpg(virtualaddress); // Invalidate the address
    free_frame(phys_addr); // Free the underlying physical frame

    return true;
}

// Set writable flag on a page
void set_page_writable(void* virtualaddress, bool writable) {
    tracelast_func("set_page_writable");
    uint64_t rip;
    GET_RIP(rip);
    enforce_max_irql(DISPATCH_LEVEL, (void*)rip);
    uint64_t va = (uint64_t)virtualaddress;

    size_t pml4_i = get_pml4_index(va);
    size_t pdpt_i = get_pdpt_index(va);
    size_t pd_i = get_pd_index(va);
    size_t pt_i = get_pt_index(va);

    uint64_t* pml4 = pml4_from_recursive();

    if (!(pml4[pml4_i] & PAGE_PRESENT)) return;
    uint64_t* pdpt = pdpt_from_recursive(pml4_i);

    if (!(pdpt[pdpt_i] & PAGE_PRESENT)) return;
    uint64_t* pd = pd_from_recursive(pml4_i, pdpt_i);

    if (!(pd[pd_i] & PAGE_PRESENT)) return;
    uint64_t* pt = pt_from_recursive(pml4_i, pdpt_i, pd_i);

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
        invlpg((void*)va);
    }
}

void set_page_user_access(void* virtualaddress, bool user_accessible) {
    tracelast_func("set_page_user_access");
    uint64_t rip;
    GET_RIP(rip);
    enforce_max_irql(DISPATCH_LEVEL, (void*)rip);
    uint64_t va = (uint64_t)virtualaddress;

    size_t pml4_i = get_pml4_index(va);
    size_t pdpt_i = get_pdpt_index(va);
    size_t pd_i = get_pd_index(va);
    size_t pt_i = get_pt_index(va);

    uint64_t* pml4 = pml4_from_recursive();

    if (!(pml4[pml4_i] & PAGE_PRESENT)) return;
    uint64_t* pdpt = pdpt_from_recursive(pml4_i);
    if (!(pdpt[pdpt_i] & PAGE_PRESENT)) return;
    uint64_t* pd = pd_from_recursive(pml4_i, pdpt_i);
    if (!(pd[pd_i] & PAGE_PRESENT)) return;
    uint64_t* pt = pt_from_recursive(pml4_i, pdpt_i, pd_i);
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
        invlpg((void*)va);
    }
}

bool MtIsAddressValid(void* virtualAddr) {
    tracelast_func("MtIsAddressValid");
    uint64_t rip;
    GET_RIP(rip);
    enforce_max_irql(DISPATCH_LEVEL, (void*)rip);

    uint64_t va = (uint64_t)virtualAddr;

    size_t pml4_i = get_pml4_index(va);
    size_t pdpt_i = get_pdpt_index(va);
    size_t pd_i = get_pd_index(va);
    size_t pt_i = get_pt_index(va);

    uint64_t* pml4 = pml4_from_recursive();
    if (!(pml4[pml4_i] & PAGE_PRESENT)) return false;

    // advance
    uint64_t* pdpt = pdpt_from_recursive(pml4_i);
    if (!(pdpt[pdpt_i] & PAGE_PRESENT)) return false;

    uint64_t* pd = pd_from_recursive(pml4_i, pdpt_i);
    if (!(pd[pd_i] & PAGE_PRESENT)) return false;

    uint64_t* pt = pt_from_recursive(pml4_i, pdpt_i, pd_i);
    if (!(pt[pt_i] & PAGE_PRESENT)) return false;

    return true;
}
