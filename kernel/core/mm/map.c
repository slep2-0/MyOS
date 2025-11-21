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
#include "../../assert.h"

static inline uint64_t canonical_high(uint64_t addr) {
    // If bit 47 is set, set all higher bits
    if (addr & (1ULL << 47)) {
        return addr | 0xFFFF000000000000ULL;
    }
    return addr;
}

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

FORCEINLINE_NOHEADER
PMMPTE 
MiGetPtePointer (
    IN  uintptr_t va
) 

/*++

    Routine description : Retrieves the pointer to the PTE from the virtual address given

    Arguments:

        [IN]    Virtual Address.

    Return Values:

        Pointer to PTE associated with the Virtual Address. (NULL if failure)

--*/

{
    size_t pml4_i = get_pml4_index(va);
    size_t pdpt_i = get_pdpt_index(va);
    size_t pd_i = get_pd_index(va);
    size_t pt_i = get_pt_index(va);

    uint64_t* pml4_va = pml4_from_recursive();
    if (!(pml4_va[pml4_i] & PAGE_PRESENT)) return NULL;

    uint64_t* pdpt_va = pdpt_from_recursive(pml4_i);
    if (!(pdpt_va[pdpt_i] & PAGE_PRESENT)) return NULL;

    uint64_t* pd_va = pd_from_recursive(pml4_i, pdpt_i);
    if (!(pd_va[pd_i] & PAGE_PRESENT)) return NULL;

    uint64_t* pt_va = pt_from_recursive(pml4_i, pdpt_i, pd_i);
    return (PMMPTE) & pt_va[pt_i];  // pointer to PTE
}

FORCEINLINE_NOHEADER
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
    if (!pte) return 0;
    uintptr_t phys = PTE_TO_PHYSICAL(pte);
    return PPFN_TO_INDEX(PHYSICAL_TO_PPFN(phys));
}

FORCEINLINE
uint64_t
MiTranslatePteToVa(
    IN PMMPTE pte
)

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

FORCEINLINE_NOHEADER
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
    newPte.Soft.Present = 0;

    // Write new values.
    newPte.Soft.PageFrameNumber = pfn;
    newPte.Soft.Transition = 1;

    // Exchange now.
    InterlockedExchangeU64((volatile uint64_t*)pte, newPte.Value);

    // Invalidate TLBs
    if (origVa) invlpg((void*)origVa);
    else MiReloadTLBs();

    // Return.
    return;
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

        The physical address is returned with its equivalent offset (so not page aligned). (e.g VA = 0xff8880 Phys = 0x4080)

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