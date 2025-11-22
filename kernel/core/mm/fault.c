/*++

Module Name:

    map.c

Purpose:

    This translation unit contains the implementation of access faults in the system. (page faults)

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "../../includes/mm.h"
#include "../../includes/me.h"
#include "../../includes/ps.h"
#include "../../includes/mg.h"

MTSTATUS
MmAccessFault(
    IN  uint64_t FaultBits,
    IN  uint64_t VirtualAddress,
    IN  PRIVILEGE_MODE PreviousMode,
    IN  PTRAP_FRAME TrapFrame
)

/*++

    Routine description:

        This function is called by the kernel on data or instruction access faults.
        - The access fault was detected due to:
            An Access Violation.
            A PTE with the present bit clear.
            A Valid PTE with the Dirty bit and a write operation.

        Note that the page fault could occur because of the Page Directory contents as well.

        This routine determines what type of fault it is and calls the appropriate routine to handle or write the page fault.

    Arguments:

        [IN]    FaultBits - The error code pushed by the CPU.
        [IN]    VirtualAddress - The Memory Address Referenced (CR2)
        [IN]    PreviousMode - Supplies the mode (kernel or user) where the fault occured.
        [IN]    TrapFrame - Trap information at fault.

    Return Values:

        MTSTATUS Code resulting in the status of fault handling operation.

        Could be:
            MT_SUCCESS
            MT_ACCESS_VIOLATION
            MT_GUARD_PAGE_VIOLATION

--*/

{
    // Declarations
    // previrql
    PMMPTE ReferencedPte = MiGetPtePointer(VirtualAddress);
    FAULT_OPERATION OperationDone = MiRetrieveOperationFromErrorCode(FaultBits);
    IRQL PreviousIrql = MeGetCurrentIrql();
#ifdef DEBUG
    gop_printf(COLOR_RED, "Inside MmAccessFault | FaultBits: %x | VirtualAddress: %p | PreviousMode: %d | TrapFrame: %p | Operation: %d | Irql: %d", FaultBits, VirtualAddress, PreviousMode, TrapFrame, OperationDone, PreviousIrql);
#endif
    if (!ReferencedPte) {
        // If we cannot get the PTE for the VA, we raise access violation if its user mode, or bugcheck on kernel mode.
        if (PreviousMode == UserMode) {
            return MT_ACCESS_VIOLATION;
        }

        goto PageFaultBugCheck;
    }

    // If the VA given isn't canonical (sign extended after bit 47, required by CPU MMU Laws), we return or bugcheck depending on the previous mode.
    if (!MI_IS_CANONICAL_ADDR(VirtualAddress)) {

        if (PreviousMode == UserMode) {
            // User mode fault on non canonical address, not destructive.
            return MT_ACCESS_VIOLATION;
        }

        // Kernel mode page fault on a non canonical address.
        goto PageFaultBugCheck;

    }

    // Now we check for each address in the system, and handle the request based on that.
    if (VirtualAddress >= MmSystemRangeStart) {
        if (PreviousMode == UserMode) {
            // User mode access in kernel memory, invalid.
            return MT_ACCESS_VIOLATION;
        }

        MMPTE TempPte = *ReferencedPte;

        // PTE Is present, but we got a fault.
        if (TempPte.Hard.Present) {
            // Write fault to read-only memory.
            if ((OperationDone == WriteOperation) && (TempPte.Hard.Write == 0)) {
                MeBugCheckEx(
                    ATTEMPTED_WRITE_TO_READONLY_MEMORY,
                    (void*)VirtualAddress,
                    (void*)ReferencedPte,
                    NULL,
                    NULL
                );
            }

            // If we get here, it was an access/dirty update — set dirty bit if write
            if (OperationDone == WriteOperation) {
                // set dirty in PTE and, if needed, PFN->Dirty
                // Prefer an atomic update: build NewPte = TempPte; NewPte.Hard.Dirty = 1; WriteValidPteAtomic(...)
                MMPTE NewPte = TempPte;
                NewPte.Hard.Dirty = 1;
                MiAtomicExchangePte(ReferencedPte, NewPte.Value);
            }
            return MT_SUCCESS;
        }
        
        // Before any demand allocation, check IRQL.
        if (PreviousIrql > DISPATCH_LEVEL) {
            // IRQL Isn't less or equal to DISPATCH_LEVEL, so we cannot lazily allocate, since it would **block**.
            MeBugCheckEx(
                IRQL_NOT_LESS_OR_EQUAL,
                (void*)VirtualAddress,
                (void*)PreviousIrql,
                (void*)OperationDone,
                (void*)TrapFrame->rip
            );
        }
        

        // PTE Isn't present, check for demand allocations.
        if (MM_IS_DEMAND_ZERO_PTE(TempPte)) {
            // Allocate a physical page for kernel demand-zero
            PAGE_INDEX pfn = MiRequestPhysicalPage(PfnStateZeroed);
            if (pfn == PFN_ERROR) {
                // out of memory.
                goto PageFaultBugCheck;
            }

            // Check protection mask.
            uint64_t ProtectionFlags = PAGE_PRESENT;
            ProtectionFlags |= (TempPte.Soft.SoftwareFlags & PROT_KERNEL_WRITE) ? PAGE_RW : 0;

            // Write the PTE.
            MI_WRITE_PTE(ReferencedPte, VirtualAddress, PPFN_TO_PHYSICAL_ADDRESS(INDEX_TO_PPFN(pfn)), ProtectionFlags);
            
            return MT_SUCCESS;
        }

        // PTE Isn't present, and its a transition
        if (TempPte.Soft.Transition == 1) {
            // Retrieve the PFN Number written in the transition page.
            PAGE_INDEX pfn = TempPte.Soft.PageFrameNumber;
            if (!MiIsValidPfn(pfn)) goto PageFaultBugCheck;

            // Check protection mask.
            uint64_t ProtectionFlags = PAGE_PRESENT;
            ProtectionFlags |= (TempPte.Soft.SoftwareFlags & PROT_KERNEL_WRITE) ? PAGE_RW : 0;

            MI_WRITE_PTE(ReferencedPte, VirtualAddress, PFN_TO_PHYS(pfn), ProtectionFlags);

            return MT_SUCCESS;
        }

        // TODO GRAB FROM PAGEFILE

        // Unknown PTE format -> bugcheck (kernel space)
        goto PageFaultBugCheck;
    }

    // Address is below the kernel start, and above user address.
    if (VirtualAddress > MmHighestUserAddress && VirtualAddress < MmSystemRangeStart) {
        if (PreviousMode == UserMode) return MT_ACCESS_VIOLATION;
        goto PageFaultBugCheck;
    }

    // Address is in user range.
    if (VirtualAddress <= MmHighestUserAddress) {
        if (PreviousMode == KernelMode) {
            // Kernel mode dereference on a user address
            goto PageFaultBugCheck;
        }

        // User mode fault on a user address, we check if there is a vad for it, if so, allocate the page.
        PMMVAD vad = MiFindVad(PsGetCurrentProcess()->VadRoot, VirtualAddress);
        if (!vad) return MT_ACCESS_VIOLATION;

        // Looks like we have a valid vad, lets allocate.
        PAGE_INDEX pfn = MiRequestPhysicalPage(PfnStateFree);
        if (pfn == PFN_ERROR) goto PageFaultBugCheck; // TODO OOM

        // Acquire the PTE for the faulty VA.
        PMMPTE pte = MiGetPtePointer(VirtualAddress);

        // Write the PTE.
        MI_WRITE_PTE(pte, VirtualAddress, PFN_TO_PHYS(pfn), PAGE_PRESENT | PAGE_RW);

        // Return success.
        return MT_SUCCESS;
    }

    // Address, is, what... impossible!
    // This comment means execution is impossible to reach here, as we sanitized all addresses in the 48bit paging hierarchy.
    // If it does reach here, look below.

PageFaultBugCheck:
    MeBugCheckEx(
        PAGE_FAULT,
        (void*)VirtualAddress,
        (void*)MiRetrieveOperationFromErrorCode(TrapFrame->error_code),
        (void*)TrapFrame->rip,
        (void*)TrapFrame->error_code
    );
}

bool
MmInvalidAccessAllowed(
    void
)

/*++

    Routine description:
        (UNUSED, ALWAYS FALSE)
        This function determines if invalid access (e.g, a null pointer dereference), is allowed within the current context.

    Arguments:

        None.

    Return Values:

        True if invalid access is allowed, false otherwise.

    Notes:

        This routine is unused, but will be kept for future modifications if any.

--*/


{
    return false;
}