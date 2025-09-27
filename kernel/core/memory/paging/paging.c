/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     64-bit Memory Paging Implementation (4-level paging)
 */

#include "paging.h"
#include "../../interrupts/idt.h"
#include "../memory.h"
#include "../../../drivers/ahci/ahci.h"

// From now on, physical addresses are uintptr_t, and virtual addresses are void pointers.

static inline uint64_t canonical_high(uint64_t addr) {
    // If bit 47 is set, set all higher bits
    if (addr & (1ULL << 47)) {
        return addr | 0xFFFF000000000000ULL;
    }
    return addr;
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

// Extract indices from virtual address
static inline size_t get_pml4_index(uint64_t va) { return (va >> 39) & 0x1FF; }
static inline size_t get_pdpt_index(uint64_t va) { return (va >> 30) & 0x1FF; }
static inline size_t get_pd_index(uint64_t va) { return (va >> 21) & 0x1FF; }
static inline size_t get_pt_index(uint64_t va) { return (va >> 12) & 0x1FF; }

static inline size_t get_offset(uint64_t va) {
    return va & 0xFFF;
}

// Input pt[pt_i] into the function
static inline uintptr_t get_frame_base(uint64_t pt_pti) {
    return pt_pti & 0xFFFFFFFFF000;
}

static void map_range_higher(uintptr_t phys_start, uintptr_t phys_end, void* va_start, uint64_t flags) {
    uintptr_t p = phys_start;
    uintptr_t v = (uintptr_t)va_start;

    for (; p < phys_end; p += PAGE_SIZE_4K, v += PAGE_SIZE_4K) {
        map_page((void*)v, p, flags);
    }
}

// Corrected map_page function
void map_page(void* virtualaddress, uintptr_t physicaladdress, uint64_t flags) {
    tracelast_func("map_page");
    // TODO: Versatile mapping, know when to bail out when it's mapped already, and when to continue a force remap.
    if (MtIsAddressValid(virtualaddress)) return;
    uint64_t va = (uint64_t)virtualaddress;
    uintptr_t pa = physicaladdress;
    BUGCHECK_ADDITIONALS addt = { 0 };
    size_t pml4_i = get_pml4_index(va);
    size_t pdpt_i = get_pdpt_index(va);
    size_t pd_i = get_pd_index(va);
    size_t pt_i = get_pt_index(va);

    uint64_t* pml4_va = pml4_from_recursive();

    // 2. Ensure PDPT exists
    if (!(pml4_va[pml4_i] & PAGE_PRESENT)) {
        ksnprintf(addt.str, sizeof(addt.str), "In PML4, VA: %p, PA: %p, FLAGS: %d", virtualaddress, physicaladdress, flags);
        MtBugcheckEx(NULL, NULL, BAD_PAGING, &addt, true);
    }
    uint64_t* pdpt_va = pdpt_from_recursive(pml4_i);

    // 3. Ensure PD exists
    if (!(pdpt_va[pdpt_i] & PAGE_PRESENT)) {
        ksnprintf(addt.str, sizeof(addt.str), "In PDPT, VA: %p, PA: %p, FLAGS: %d", virtualaddress, physicaladdress, flags);
        MtBugcheckEx(NULL, NULL, BAD_PAGING, &addt, true);
    }
    uint64_t* pd_va = pd_from_recursive(pml4_i, pdpt_i);

    // 4. Ensure PT exists
    if (!(pd_va[pd_i] & PAGE_PRESENT)) {
        ksnprintf(addt.str, sizeof(addt.str), "In PD, VA: %p, PA: %p, FLAGS: %d", virtualaddress, physicaladdress, flags);
        MtBugcheckEx(NULL, NULL, BAD_PAGING, &addt, true);
    }
    uint64_t* pt_va = pt_from_recursive(pml4_i, pdpt_i, pd_i);

    // 5. Map the page
    pt_va[pt_i] = (pa & ~0xFFFULL) | flags;

    // 6. Flush the TLB for the target virtual address
    invlpg(virtualaddress);
}

bool unmap_page(void* virtualaddress) {
    tracelast_func("unmap_page");
    uint64_t rip;
    GET_RIP(rip);
    enforce_max_irql(DISPATCH_LEVEL, (void*)rip);

    /* 1) canonicalize VA immediately (avoids invlpg #GP on non-canonical) */
    uint64_t va = (uint64_t)virtualaddress;
    va = canonical_high(va);

    size_t pml4_i = get_pml4_index(va);
    size_t pdpt_i = get_pdpt_index(va);
    size_t pd_i = get_pd_index(va);
    size_t pt_i = get_pt_index(va);

    uint64_t* pml4 = pml4_from_recursive();
    if (!(pml4[pml4_i] & PAGE_PRESENT)) {
        /* not mapped at this level */
        return false;
    }

    uint64_t* pdpt = pdpt_from_recursive(pml4_i);
    uint64_t pdpt_entry = pdpt[pdpt_i];

    /* 1GiB page? handle and return */
    if (pdpt_entry & PAGE_PS) {
        uintptr_t base = (uintptr_t)(pdpt_entry & ~((1ULL << 30) - 1));
        pdpt[pdpt_i] = 0;               /* clear PDPT entry */
        invlpg((void*)va);              /* flush TLB for VA */
        free_frame(base);               /* free phys base */
        return true;
    }

    if (!(pdpt_entry & PAGE_PRESENT)) {
        return false;
    }

    uint64_t* pd = pd_from_recursive(pml4_i, pdpt_i);
    uint64_t pd_entry = pd[pd_i];

    /* 2MiB page? handle and return */
    if (pd_entry & PAGE_PS) {
        uintptr_t base = (uintptr_t)(pd_entry & ~((1ULL << 21) - 1));
        pd[pd_i] = 0;                    /* clear PD entry */
        invlpg((void*)va);
        free_frame(base);
        return true;
    }

    if (!(pd_entry & PAGE_PRESENT)) {
        return false;
    }

    /* Now it's safe to get PT pointer and examine PTE */
    uint64_t* pt = pt_from_recursive(pml4_i, pdpt_i, pd_i);

    /* If PTE not present, nothing to unmap */
    uint64_t pte = pt[pt_i];
    if (!(pte & PAGE_PRESENT)) {
        return false;
    }

    /* Extract phys and clear the PTE BEFORE freeing the frame. */
    uintptr_t phys_addr = (uintptr_t)(pte & ~0xFFFULL);
    pt[pt_i] = 0;               /* clear mapping */
    invlpg((void*)va);         /* flush TLB for VA */
    free_frame(phys_addr);     /* free physical frame */

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

void MtAddPageFlags(void* virtualaddress, uint64_t flags) {
    tracelast_func("set_page_flags");
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
    entry |= flags;
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
    /* TODO Uncomment when a PFN database (struct) is available, so that it would not be accessed when on a DISPATCH_LEVEL irql. (also when it gets paged out to disk)
    uint64_t rip;
    GET_RIP(rip);
    enforce_max_irql(DISPATCH_LEVEL, (void*)rip);
    */
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

uintptr_t MtTranslateVirtualToPhysical(void* virtualaddress) {
    uint64_t va = (uint64_t)virtualaddress;
    size_t pml4_i = get_pml4_index(va), pdpt_i = get_pdpt_index(va);
    size_t pd_i = get_pd_index(va), pt_i = get_pt_index(va);
    size_t offset = get_offset(va);

    uint64_t* pml4 = pml4_from_recursive();
    if (!(pml4[pml4_i] & PAGE_PRESENT)) return 0;

    uint64_t* pdpt = pdpt_from_recursive(pml4_i);
    uint64_t pdpt_entry = pdpt[pdpt_i];
    if (!(pdpt_entry & PAGE_PRESENT)) return 0;
    // 1GiB page?
    if (pdpt_entry & PAGE_PS) {
        uintptr_t base = (pdpt_entry & ~((1ULL << 30) - 1)); // mask lower 30 bits
        return base + (va & ((1ULL << 30) - 1));
    }

    uint64_t* pd = pd_from_recursive(pml4_i, pdpt_i);
    uint64_t pd_entry = pd[pd_i];
    if (!(pd_entry & PAGE_PRESENT)) return 0;
    // 2MiB page?
    if (pd_entry & PAGE_PS) {
        uintptr_t base = pd_entry & ~((1ULL << 21) - 1);
        return base + (va & ((1ULL << 21) - 1));
    }

    uint64_t* pt = pt_from_recursive(pml4_i, pdpt_i, pd_i);
    uint64_t pt_entry = pt[pt_i];
    if (!(pt_entry & PAGE_PRESENT)) return 0;

    uintptr_t base = pt_entry & ~0xFFFULL;
    return base + offset;
}