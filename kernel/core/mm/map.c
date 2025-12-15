/*++

Module Name:

    map.c

Purpose:

    This translation unit contains the implementation of the internal mapping functions for kernel use.

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "../../includes/mm.h"
#include "../../includes/mh.h"
#include "../../assert.h"

static inline uint64_t canonical_high(uint64_t addr) {
    // If bit 47 is set, set all higher bits
    if (addr & (1ULL << 47)) {
        return addr | 0xFFFF000000000000ULL;
    }
    return addr;
}

uint64_t* pml4_from_recursive(void) {
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

PMMPTE
MiGetPtePointer(
    IN  uintptr_t va
)

/*++

    Routine description : Retrieves the pointer to the PTE from the virtual address given

    Arguments:

        [IN]    Virtual Address.

    Return Values:

        Pointer to PTE associated with the Virtual Address. (NULL if out of memory)

--*/

{
    // 1. Calculate Indices
    size_t pml4_i = get_pml4_index(va);
    size_t pdpt_i = get_pdpt_index(va);
    size_t pd_i = get_pd_index(va);
    size_t pt_i = get_pt_index(va);

    uint64_t* pml4_va = pml4_from_recursive();
    if (!(pml4_va[pml4_i] & PAGE_PRESENT)) {
        // Allocate a new PDPT
        PAGE_INDEX pfn = MiRequestPhysicalPage(PfnStateZeroed);
        if (pfn == PFN_ERROR) return NULL;

        // We are modifying the recursive mapping of the PML4 entry.
        PMMPTE pml4e = (PMMPTE)&pml4_va[pml4_i];
        MI_WRITE_PTE(pml4e, pdpt_from_recursive(pml4_i), PFN_TO_PHYS(pfn), PAGE_PRESENT | PAGE_RW);
    }

    uint64_t* pdpt_va = pdpt_from_recursive(pml4_i);
    if (!(pdpt_va[pdpt_i] & PAGE_PRESENT)) {
        // Allocate a new Page Directory
        PAGE_INDEX pfn = MiRequestPhysicalPage(PfnStateZeroed);
        if (pfn == PFN_ERROR) return NULL;

        // Link new PD into PDPT
        PMMPTE pdpte = (PMMPTE)&pdpt_va[pdpt_i];
        MI_WRITE_PTE(pdpte, pd_from_recursive(pml4_i, pdpt_i), PFN_TO_PHYS(pfn), PAGE_PRESENT | PAGE_RW);
    }

    uint64_t* pd_va = pd_from_recursive(pml4_i, pdpt_i);
    if (!(pd_va[pd_i] & PAGE_PRESENT)) {
        // Allocate a new Page Table
        PAGE_INDEX pfn = MiRequestPhysicalPage(PfnStateZeroed);
        if (pfn == PFN_ERROR) return NULL;

        // Link new PT into PD
        PMMPTE pde = (PMMPTE)&pd_va[pd_i];
        MI_WRITE_PTE(pde, pt_from_recursive(pml4_i, pdpt_i, pd_i), PFN_TO_PHYS(pfn), PAGE_PRESENT | PAGE_RW);
    }

    // Return addr of PTE.
    uint64_t* pt_va = pt_from_recursive(pml4_i, pdpt_i, pd_i);
    return (PMMPTE)&pt_va[pt_i];
}

void
MiInvalidateTlbForVa(
    IN void* VirtualAddress
)

/*++

    Routine description:

        Invalidates CPUs TLB for the specified virtual address.

    Arguments:

        [IN]    void* VirtualAddress - Virtual address to flush for.

    Return Values:

        None.

    Notes:
        
        On the SMP Build, if APs are active, an IPI is sent to flush their TLB for the VA as well.

--*/

{
    invlpg(VirtualAddress);
    // If SMP is initialized, send IPI.
#ifndef MT_UP
    if (smpInitialized) {
        IPI_PARAMS Param;
        Param.pageParams.addressToInvalidate = (uint64_t)VirtualAddress;
        MhSendActionToCpusAndWait(CPU_ACTION_PERFORM_TLB_SHOOTDOWN, Param);
    }
#endif
}

PAGE_INDEX
MiTranslatePteToPfn (
    IN  PMMPTE pte
)

/*++

    Routine description:
        
        Translates the PTE given into the appropriate PFN behind its physical address.

    Arguments:

        [IN]    pte - Pointer to MMPTE PTE in memory.

    Return Values:

        Page Frame Index.

--*/

{
    if (!pte) return PFN_ERROR;
    uintptr_t phys = PTE_TO_PHYSICAL(pte);
    return PPFN_TO_INDEX(PHYSICAL_TO_PPFN(phys));
}

uint64_t
MiTranslatePteToVa(
    IN PMMPTE pte
)

/*++

    Routine description:

        Translates the PTE given to its appropriate virtual address.

    Arguments:

        [IN]    pte - Pointer to MMPTE PTE in memory.

    Return Values:

        Virtual Address associated with the PTE.

    Notes:

        The only reason this works is because the method used to find the indices for the VA (pml4, pdpt, pd, pt, pte) 
        Is reversible, since it is bit shifting.

--*/

{
    uintptr_t p = (uintptr_t)pte;

    size_t pml4_check = (p >> 39) & 0x1FF;
    if (pml4_check != RECURSIVE_INDEX) {
        /* not a recursive PTE pointer */
        return (uintptr_t)0;
    }

    size_t pml4_i = (p >> 30) & 0x1FF;
    size_t pdpt_i = (p >> 21) & 0x1FF;
    size_t pd_i = (p >> 12) & 0x1FF;
    size_t pt_i = (p >> 3) & 0x1FF; /* pt entry index */

    uint64_t va = ((uint64_t)pml4_i << 39) |
        ((uint64_t)pdpt_i << 30) |
        ((uint64_t)pd_i << 21) |
        ((uint64_t)pt_i << 12);

    return canonical_high(va); /* page-aligned VA for invlpg */
}

void
MiUnmapPte (
    IN  PMMPTE pte
)

/*++

    Routine description:

        Unmaps the pte from the current address pace.

    Arguments:

        [IN]    pte - Pointer to MMPTE PTE in memory.

    Return Values:

        None.

    Notes:

        This function DOES NOT release the PFN associated with the PTE back to the database, you must do so yourself.

--*/

{
    if (!pte) return;
    // First gets its PFN to write to the PMMPTE PresentNotSet union.
    PAGE_INDEX pfn = MiTranslatePteToPfn(pte);
    if (!pfn) return;
    // Get the PTE's original VA.
    uint64_t origVa = MiTranslatePteToVa(pte);

    // Atomically exchange old info with new info to avoid races.
    MMPTE newPte;
    
    // Zero out newPte
    kmemset(&newPte, 0, sizeof(MMPTE));

    // Write new values.
    newPte.Soft.PageFrameNumber = pfn;
    
    // I removed the transition set here, even though it has a PFN assigned to it (to track last good PFN), we don't mark it as transition.
    // Instead, when we put it in the standby list, there should be a unique function for it. TODO

    // Exchange now.
    InterlockedExchangeU64((volatile uint64_t*)pte, newPte.Value);

    // Invalidate TLBs
    if (origVa) MiInvalidateTlbForVa((void*)origVa);
    else MiReloadTLBs();

    // Return.
    return;
}


// Reloads CR3 to flush all TLBs (slow flush)
void
MiReloadTLBs(
    void
)

{
    __write_cr3(__read_cr3());
#ifndef MT_UP
    IPI_PARAMS param;
    MhSendActionToCpusAndWait(CPU_ACTION_FLUSH_CR3, param);
#endif
}

uintptr_t
MiTranslateVirtualToPhysical(
    IN void* VirtualAddress
)

/*++

    Routine description:

        Translates the given virtual address to its equivalent (**IF MAPPED TO**) physical address.

    Arguments:

        [IN]    void* VirtualAddress - The mapped virtual address.

    Return Values:

        The physical address mapped to the virtual address, or 0 if invalid.

        The physical address is returned with its equivalent offset (so not page aligned, maybe, modulus the VA given to check.). (e.g VA = 0xff8880 Phys = 0x4880)

--*/

{
    PMMPTE pte = MiGetPtePointer((uintptr_t)VirtualAddress);
    if (!pte) return 0;

    if (!pte->Hard.Present) return 0;

    return (uintptr_t)PTE_TO_PHYSICAL(pte) + VA_OFFSET(VirtualAddress);
}

bool
MmIsAddressPresent(
    IN  uintptr_t VirtualAddress
)

/*++

    Routine description:

        Checks if the given address is currently present in memory (won't cause a page fault on access)

    Arguments:

        [IN]    uintptr_t VirtualAddress - The virtual address.

    Return Values:

        True if the address is valid and in memory, false otherwise.

--*/

{
    PMMPTE pte = MiGetPtePointer(VirtualAddress);
    return pte->Hard.Present;
}