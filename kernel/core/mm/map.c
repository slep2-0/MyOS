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

static inline uint64_t canonical_high(uint64_t addr)

/*++

    Routine description:

        Builds the canonical high-half form of a virtual address.

    Arguments:

        [IN] addr - Virtual or physical address to transform.

    Return Values:

        The canonicalized virtual address.

--*/

{
    // If bit 47 is set, set all higher bits
    if (addr & (1ULL << 47)) {
        return addr | 0xFFFF000000000000ULL;
    }
    return addr;
}

uint64_t* pml4_from_recursive(void)

/*++

    Routine description:

        Returns the recursively mapped PML4 entry for a virtual address.

    Arguments:

        None.

    Return Values:

        A pointer to the resulting object or storage, or NULL when no result is available.

--*/

{
    uint64_t va = ((uint64_t)RECURSIVE_INDEX << 39) |
        ((uint64_t)RECURSIVE_INDEX << 30) |
        ((uint64_t)RECURSIVE_INDEX << 21) |
        ((uint64_t)RECURSIVE_INDEX << 12);
    va = canonical_high(va);
    return (uint64_t*)(uintptr_t)va;
}

static inline uint64_t* pdpt_from_recursive(size_t pml4_i)

/*++

    Routine description:

        Returns the recursively mapped page-directory-pointer entry for a virtual address.

    Arguments:

        [IN] pml4_i - PML4 index in the recursive mapping.

    Return Values:

        A pointer to the resulting object or storage, or NULL when no result is available.

--*/

{
    uint64_t va = ((uint64_t)RECURSIVE_INDEX << 39) |
        ((uint64_t)RECURSIVE_INDEX << 30) |
        ((uint64_t)RECURSIVE_INDEX << 21) |
        ((uint64_t)pml4_i << 12); // <-- CORRECTED
    va = canonical_high(va);
    return (uint64_t*)(uintptr_t)va;
}

// To get PD page for pml4_i, pdpt_i
static inline uint64_t* pd_from_recursive(size_t pml4_i, size_t pdpt_i)

/*++

    Routine description:

        Returns the recursively mapped page-directory entry for a virtual address.

    Arguments:

        [IN] pml4_i - PML4 index in the recursive mapping.
        [IN] pdpt_i - Page-directory-pointer index in the recursive mapping.

    Return Values:

        A pointer to the resulting object or storage, or NULL when no result is available.

--*/

{
    uint64_t va = ((uint64_t)RECURSIVE_INDEX << 39) |
        ((uint64_t)RECURSIVE_INDEX << 30) |
        ((uint64_t)pml4_i << 21) |        // <-- CORRECTED
        ((uint64_t)pdpt_i << 12);       // <-- CORRECTED
    va = canonical_high(va);
    return (uint64_t*)(uintptr_t)va;
}

// To get PT page for pml4_i, pdpt_i, pd_i
static inline uint64_t* pt_from_recursive(size_t pml4_i, size_t pdpt_i, size_t pd_i)

/*++

    Routine description:

        Returns the recursively mapped page-table entry for a virtual address.

    Arguments:

        [IN] pml4_i - PML4 index in the recursive mapping.
        [IN] pdpt_i - Page-directory-pointer index in the recursive mapping.
        [IN] pd_i - Page-directory index in the recursive mapping.

    Return Values:

        A pointer to the resulting object or storage, or NULL when no result is available.

--*/

{
    uint64_t va = ((uint64_t)RECURSIVE_INDEX << 39) |
        ((uint64_t)pml4_i << 30) |
        ((uint64_t)pdpt_i << 21) |
        ((uint64_t)pd_i << 12);
    va = canonical_high(va);
    return (uint64_t*)(uintptr_t)va;
}

// Extract indices from virtual address
static inline size_t get_pml4_index(uint64_t va)

/*++

    Routine description:

        Extracts the PML4 index from a virtual address.

    Arguments:

        [IN] va - Virtual address whose paging index or entry is requested.

    Return Values:

        The PML4 index for the virtual address.

--*/

{ return (va >> 39) & 0x1FF; }
static inline size_t get_pdpt_index(uint64_t va)

/*++

    Routine description:

        Extracts the page-directory-pointer index from a virtual address.

    Arguments:

        [IN] va - Virtual address whose paging index or entry is requested.

    Return Values:

        The page-directory-pointer index for the virtual address.

--*/

{ return (va >> 30) & 0x1FF; }
static inline size_t get_pd_index(uint64_t va)

/*++

    Routine description:

        Extracts the page-directory index from a virtual address.

    Arguments:

        [IN] va - Virtual address whose paging index or entry is requested.

    Return Values:

        The page-directory index for the virtual address.

--*/

{ return (va >> 21) & 0x1FF; }
static inline size_t get_pt_index(uint64_t va)

/*++

    Routine description:

        Extracts the page-table index from a virtual address.

    Arguments:

        [IN] va - Virtual address whose paging index or entry is requested.

    Return Values:

        The page-table index for the virtual address.

--*/

{ return (va >> 12) & 0x1FF; }

static
bool
MiEnsureIntermediateTable(
    IN PMMPTE Entry,
    IN uintptr_t RecursiveAddress,
    IN uint64_t Flags
)

/*++

    Routine description:

        Ensures an intermediate paging structure exists for a mapping operation.

    Arguments:

        [IN] Entry - List, table, or object entry affected by the routine.
        [IN] RecursiveAddress - Recursive virtual address of the page-table level.
        [IN] Flags - Flags controlling the operation.

    Return Values:

        true when the intermediate table is present or created, or false on allocation failure.

--*/

{
    for (;;) {
        uint64_t Existing = InterlockedLoadAcquire(&Entry->Value);
        if (Existing & PAGE_PRESENT) return true;

        PAGE_INDEX PfnIndex = MiRequestPhysicalPage(PfnStateZeroed);
        if (PfnIndex == PFN_ERROR) return false;

        PPFN_ENTRY Pfn = INDEX_TO_PPFN(PfnIndex);
        Pfn->Descriptor.Mapping.PteAddress = Entry;
        Pfn->Descriptor.Mapping.Vad = NULL;
        Pfn->State = PfnStateActive;
        Pfn->Flags = PFN_FLAG_NONPAGED;

        uint64_t NewValue = PFN_TO_PHYS(PfnIndex) | Flags;
        if (MiAtomicSetPte(Entry, NewValue, Existing)) {
            MiInvalidateTlbForVa((void*)RecursiveAddress);
            return true;
        }

        // Another CPU installed or changed this level first. Our private,
        // still-unpublished page can go straight back to the PFN allocator.
        Pfn->Descriptor.Mapping.PteAddress = NULL;
        Pfn->Descriptor.Mapping.Vad = NULL;
        Pfn->State = PfnStateTransition;
        Pfn->Flags = PFN_FLAG_NONE;
        MiReleasePhysicalPage(PfnIndex);
    }
}

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

    uint64_t intermediateFlags = PAGE_PRESENT | PAGE_RW;

    // If we are touching user address space, we add user accessibility.
    if (va <= MmHighestUserAddress) {
        intermediateFlags |= PAGE_USER;
    }

    uint64_t* pml4_va = pml4_from_recursive();
    PMMPTE pml4e = (PMMPTE)&pml4_va[pml4_i];
    if (!MiEnsureIntermediateTable(pml4e,
        (uintptr_t)pdpt_from_recursive(pml4_i), intermediateFlags)) return NULL;

    uint64_t* pdpt_va = pdpt_from_recursive(pml4_i);
    PMMPTE pdpte = (PMMPTE)&pdpt_va[pdpt_i];
    if (!MiEnsureIntermediateTable(pdpte,
        (uintptr_t)pd_from_recursive(pml4_i, pdpt_i), intermediateFlags)) return NULL;

    uint64_t* pd_va = pd_from_recursive(pml4_i, pdpt_i);
    PMMPTE pde = (PMMPTE)&pd_va[pd_i];
    if (!MiEnsureIntermediateTable(pde,
        (uintptr_t)pt_from_recursive(pml4_i, pdpt_i, pd_i), intermediateFlags)) return NULL;

    // Return addr of PTE.
    uint64_t* pt_va = pt_from_recursive(pml4_i, pdpt_i, pd_i);
    return (PMMPTE)&pt_va[pt_i];
}

PMMPTE
MiGetPml4ePointer(
    IN  uintptr_t va
)

/*++

    Routine description:

        Returns the PML4E pointer for a virtual address.

    Arguments:

        [IN] va - Virtual address whose paging index or entry is requested.

    Return Values:

        A pointer to the PML4 entry for the address.

--*/

{
    // 1. Calculate Indices
    size_t pml4_i = get_pml4_index(va);

    uint64_t intermediateFlags = PAGE_PRESENT | PAGE_RW;

    // If we are touching user address space, we add user accessibility.
    if (va <= MmHighestUserAddress) {
        intermediateFlags |= PAGE_USER;
    }

    uint64_t* pml4_va = pml4_from_recursive();
    PMMPTE pml4e = (PMMPTE)&pml4_va[pml4_i];
    if (!MiEnsureIntermediateTable(pml4e,
        (uintptr_t)pdpt_from_recursive(pml4_i), intermediateFlags)) return NULL;

    return (PMMPTE) & pml4_va[pml4_i];
}

PMMPTE
MiGetPdptePointer(
    IN  uintptr_t va
)

/*++

    Routine description:

        Returns the PDPTE pointer for a virtual address.

    Arguments:

        [IN] va - Virtual address whose paging index or entry is requested.

    Return Values:

        A pointer to the page-directory-pointer entry for the address.

--*/

{
    // 1. Calculate Indices
    size_t pml4_i = get_pml4_index(va);
    size_t pdpt_i = get_pdpt_index(va);

    uint64_t intermediateFlags = PAGE_PRESENT | PAGE_RW;

    // If we are touching user address space, we add user accessibility.
    if (va <= MmHighestUserAddress) {
        intermediateFlags |= PAGE_USER;
    }

    uint64_t* pml4_va = pml4_from_recursive();
    PMMPTE pml4e = (PMMPTE)&pml4_va[pml4_i];
    if (!MiEnsureIntermediateTable(pml4e,
        (uintptr_t)pdpt_from_recursive(pml4_i), intermediateFlags)) return NULL;

    uint64_t* pdpt_va = pdpt_from_recursive(pml4_i);
    PMMPTE pdpte = (PMMPTE)&pdpt_va[pdpt_i];
    if (!MiEnsureIntermediateTable(pdpte,
        (uintptr_t)pd_from_recursive(pml4_i, pdpt_i), intermediateFlags)) return NULL;

    return (PMMPTE)&pdpt_va[pdpt_i];
}

PMMPTE
MiGetPdePointer(
    IN  uintptr_t va
)

/*++

    Routine description:

        Returns the PDE pointer for a virtual address.

    Arguments:

        [IN] va - Virtual address whose paging index or entry is requested.

    Return Values:

        A pointer to the page-directory entry for the address.

--*/

{
    // 1. Calculate Indices
    size_t pml4_i = get_pml4_index(va);
    size_t pdpt_i = get_pdpt_index(va);
    size_t pd_i = get_pd_index(va);

    uint64_t intermediateFlags = PAGE_PRESENT | PAGE_RW;

    // If we are touching user address space, we add user accessibility.
    if (va <= MmHighestUserAddress) {
        intermediateFlags |= PAGE_USER;
    }

    uint64_t* pml4_va = pml4_from_recursive();
    PMMPTE pml4e = (PMMPTE)&pml4_va[pml4_i];
    if (!MiEnsureIntermediateTable(pml4e,
        (uintptr_t)pdpt_from_recursive(pml4_i), intermediateFlags)) return NULL;

    uint64_t* pdpt_va = pdpt_from_recursive(pml4_i);
    PMMPTE pdpte = (PMMPTE)&pdpt_va[pdpt_i];
    if (!MiEnsureIntermediateTable(pdpte,
        (uintptr_t)pd_from_recursive(pml4_i, pdpt_i), intermediateFlags)) return NULL;

    uint64_t* pd_va = pd_from_recursive(pml4_i, pdpt_i);
    PMMPTE pde = (PMMPTE)&pd_va[pd_i];
    if (!MiEnsureIntermediateTable(pde,
        (uintptr_t)pt_from_recursive(pml4_i, pdpt_i, pd_i), intermediateFlags)) return NULL;

    return (PMMPTE)&pd_va[pd_i];
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
#ifndef MT_UP
    // If SMP is initialized, send IPI.
    if (smpInitialized) {
        IPI_PARAMS Param = { 0 };
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

    Notes:

        The PTE must be mapped to a physical address in order for this function to return an actual PFN value.
        (it doesnt have to actually BE MAPPED with the present bit, but the physical address must be written in the PTE itself)

--*/

{
    if (!pte) return PFN_ERROR;
    uintptr_t phys = PTE_TO_PHYSICAL(pte);
    return PPFN_TO_INDEX(PHYSICAL_TO_PPFN(phys));
}

uintptr_t
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

        Unmaps the pte from the current address space.

    Arguments:

        [IN]    pte - Pointer to MMPTE PTE in memory.

    Return Values:

        None.

    Notes:

        This function invalidates the TLB for the PTE, but if you manually change the PTE, you must invalidate the TLBs yourself, also, before releasing the physical page associated.

--*/

{
    if (!pte) return;

    // Get the PTE's original VA.
    uintptr_t origVa = MiTranslatePteToVa(pte);

    // Hardware and software PTE formats reuse bit positions. In particular,
    // Hard.User is Soft.Transition, so copying hardware bits after clearing
    // Present invents a transition PTE. Translate protections explicitly.
    MMPTE Expected;
    MMPTE NewPte;
    do {
        Expected.Value = InterlockedLoadAcquire(&pte->Value);
        NewPte.Value = 0;

        if (Expected.Hard.Present) {
            NewPte.Soft.SoftwareFlags |= PROT_KERNEL_READ;
            NewPte.Soft.SoftwareFlags |= Expected.Hard.Write
                ? PROT_KERNEL_WRITE : 0;
            NewPte.Soft.SoftwareFlags |= Expected.Hard.User
                ? PROT_KERNEL_USER : 0;
            NewPte.Soft.SoftwareFlags |= Expected.Hard.NoExecute
                ? PROT_KERNEL_NOEXECUTE : 0;
            NewPte.Soft.NoExecute = Expected.Hard.NoExecute;
        }
    } while (!MiAtomicSetPte(pte, NewPte.Value, Expected.Value));

    // Invalidate TLBs
    if (origVa) MiInvalidateTlbForVa((void*)origVa);
    else MiReloadTLBs();

    // Return.
    return;
}

bool MiAtomicSetPte(
    volatile PMMPTE Pte,
    uint64_t NewValue,
    uint64_t ExpectedValue
)

/*++

    Routine description:

        Atomically sets the Pte given to the new PTE if it did not change while setting.

    Arguments:

        [IN]  volatile PMMPTE Pte - Pointer to MMPTE PTE in memory.
        [IN]  uint64_t NewValue - The new value the will have if successful.
        [IN]  uint64_t ExpectedValue - The expected value the PTE should hold to identify it was not changed while setting it to new value.

    Return Values:

        True if PTE has successfuly changed to NewValue, or false if the PTE was changed between setting it.

--*/

{
    uint64_t original = InterlockedCompareExchangeU64(
        (volatile uint64_t*)Pte,
        (uint64_t)NewValue,
        (uint64_t)ExpectedValue
    );

    return (original == ExpectedValue);
}

bool
MiAtomicSetTransitionPte(
    IN PMMPTE Pte,
    IN PAGE_INDEX Pfn
)

/*++

    Routine description:

        Atomically sets the Pte given to a transition PTE if it did not change while setting.

    Arguments:

        [IN]  PMMPTE Pte - Pointer to MMPTE PTE in memory to set as transition.
        [IN]  PAGE_INDEX Pfn - The PFN number to set this PTE as a transition for.

    Return Values:

        None.

    Note:

        This function must always be called ONLY from MiReleasePhysicalPage.

--*/

{
#ifdef DEBUG
    // Assertion that the return address is within the bounds of MiReleasePhysicalPage, as this function MUST ONLY be called from there.
    // Why didnt I make it there? Because I MIGHT plan that this function can be called somewhere else, we'll see.
    assert(MiIsWithinBoundsOfReleasePhysicalPage(RETADDR(0)));
#endif

    // Set the baseline expected.
    MMPTE Expected = *Pte;

    // Runtime assertions to verify its a valid unmapped PTE before continuing.
    assert(Expected.Hard.Present == 0);
    assert(Expected.Soft.Transition == 0);

#ifdef DEBUG
    char buf[256];
    ksnprintf(buf, sizeof(buf), "Address of PTE: %p", Pte);
    assert(Expected.Hard.Prototype == 0, buf);
#endif

    // Set the transition page properties.
    MMPTE Transition = Expected;
    Transition.Soft.Transition = 1;
    Transition.Soft.PageFrameNumber = Pfn;

    // Set the protection flags, this is so the page fault handler knows the properties of the pte.
    Transition.Soft.SoftwareFlags |= (Pte->Hard.Write) ? PROT_KERNEL_WRITE : 0;
    Transition.Soft.SoftwareFlags |= (Pte->Hard.NoExecute) ? PROT_KERNEL_NOEXECUTE : 0;
    Transition.Soft.SoftwareFlags |= (Pte->Hard.User) ? PROT_KERNEL_USER : 0;

    // Compare-exchange returns the value observed before the operation. Zero
    // is a perfectly valid expected PTE, so testing this as a boolean inverts
    // the common success case.
    return InterlockedCompareExchangeU64(
        (volatile uint64_t*)Pte,
        Transition.Value,
        Expected.Value
    ) == Expected.Value;
}

// Reloads CR3 to flush all TLBs (slow flush)
// Sends IPI on SMP
void
MiReloadTLBs(
    void
)

/*++

    Routine description:

        Reloads CR3 to invalidate non-global TLB entries on the current processor.

    Arguments:

        None.

    Return Values:

        None.

--*/

{
    __write_cr3(__read_cr3());
#ifndef MT_UP
    IPI_PARAMS param = { 0 };
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

    Notes:

        This function shouldn't be used, atleast not reliably, as addresses can very well be paged out to disk.
--*/

{
    PMMPTE pte = MiGetPtePointer(VirtualAddress);
    return pte && pte->Hard.Present;
}