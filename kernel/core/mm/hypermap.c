/*++

Module Name:

    hypermap.c

Purpose:

    This translation unit contains the implementation of the temporary mapping functions. (hyperspace)/s

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "../../includes/mm.h"

// The physical memory offset itself is the hypermap virtual address. This is ruled by not touching the 0x0 - 0x1000 physical addresses AT ALL.
#define HYPERMAP_VIRTUAL_ADDRESS PhysicalMemoryOffset

SPINLOCK HyperLock;
PPFN_ENTRY g_pfnInUse;

#define LOCK_HYPERSPACE(PtrOldIrql) MsAcquireSpinlock(&HyperLock, &PtrOldIrql)
#define UNLOCK_HYPERSPACE(OldIrql) MsReleaseSpinlock(&HyperLock, OldIrql)

void*
MiMapPageInHyperspace(
    IN  uint64_t PfnIndex,
    OUT  PIRQL OldIrql
)

/*++

    Routine description:

        Temporary maps the specified PFN Page into hyperspace and returns the virtual address mapped into.

            ************************************
            *                                  *
            * Returns with a spin lock held!!! * // thanks lou
            *                                  *
            ************************************


    Arguments:

        [IN]    PfnIndex - Page frame index to map.
        [OUT]    OldIrql - Pointer to store entry IRQL.

    Return Values:

        Valid Pointer to mapped region.

--*/

{
    // First, lock the hyperspace.
    LOCK_HYPERSPACE (OldIrql);

    // Map the PFN into the page.
    PPFN_ENTRY pfn = INDEX_TO_PPFN (PfnIndex);
    uint64_t physAddr = PPFN_TO_PHYSICAL_ADDRESS (pfn);
    PMMPTE pte = MiGetPtePointer(HYPERMAP_VIRTUAL_ADDRESS);
    MI_WRITE_PTE(pte, HYPERMAP_VIRTUAL_ADDRESS, physAddr, PAGE_PRESENT | PAGE_RW);

    // Set PFN metadata.
    pfn->State = PfnStateActive;
    pfn->Descriptor.Mapping.PteAddress = pte;
    pfn->Descriptor.Mapping.Vad = NULL;
    g_pfnInUse = pfn;

    // Return the virtual address (now mapped)
    return HYPERMAP_VIRTUAL_ADDRESS;
}

void
MiUnmapHyperSpaceMap(
    IN  IRQL OldIrql
)

/*++

    Routine description:

        Unlocks the hyperspace, clears previous mapping.

    Arguments:

        [IN]    OldIrql - Entry IRQL given by MiMapPageInHyperspace

    Return Values:

        None.

    Notes:

        Does not release the PFN that was given, caller must do so.

--*/

{
    // Assertion that the hyperspace lock must be locked already (double unlock catch)
    assert((HyperLock.locked) == 1, "Double hypermap unlock");
    assert((g_pfnInUse) != 0, "No PFN when releasing hyperspace.");
    PPFN_ENTRY pfn = g_pfnInUse;
    // TODO : Clear PTE from mapping (MiUnmapPte will return early if a PFN wasnt assigned to the PTE, force unmap of PTE anyways.), to prevent use after free (of hypermap) - place code before unlocking hyperspace, so concurrency can be achieved.
    MiUnmapPte(MiGetPtePointer((uintptr_t)HYPERMAP_VIRTUAL_ADDRESS));
    
    // After MiUnmapPte changed the pfn metadata, we change it once again to invalidate it.
    pfn->Descriptor.Mapping.PteAddress = NULL;
    pfn->Descriptor.Mapping.Vad = NULL;
    pfn->State = PfnStateTransition;
    g_pfnInUse = NULL;

    // We do not release the PFN, caller must do so, because it might have other uses with it.

    // Unlock the hyperspace.
    UNLOCK_HYPERSPACE (OldIrql);
}