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
    UNREFERENCED_PARAMETER(FaultBits);

    // If the VA given isn't canonical (sign extended after bit 47, required by CPU MMU Laws), we return or bugcheck depending on the previous mode.
    if (!MI_IS_CANONICAL_ADDR(VirtualAddress)) {

        if (PreviousMode == UserMode) {
            // User mode fault on non canonical address, not destructive.
            return MT_ACCESS_VIOLATION;
        }

        // Kernel mode page fault on a non canonical address.
        goto PageFaultBugCheck;

    }

    if (VirtualAddress >= KernelVaStart) {
        // If the address referenced is above the kernel VA, we return or bugcheck depending on previous mode.
        if (PreviousMode == UserMode) {
            // User mode fault on system address.
            return MT_ACCESS_VIOLATION;
        }

        // Kernel mode page fault on a system address
        if (!MmInvalidAccessAllowed()) {
            goto PageFaultBugCheck;
        }

        // Kernel Mode Fault Allowed - Check if we have a VAD for the VA, if we do, bring a page in.
        PMMVAD vad = MiFindVad(PsGetCurrentProcess()->VadRoot, VirtualAddress);
        if (vad) {
            // Looks like we have a valid VAD for the virtual address.
            // This means this is a PagedPool (most likely) from the pool allocator.
            // Map the page, not the whole VAD.
            // Acquire a PFN from physical memory.
            PAGE_INDEX pfn = MiRequestPhysicalPage(PfnStateFree);
            if (pfn == PFN_ERROR) goto PageFaultBugCheck; // TODO OOM

            // Acquire the PTE for the faulty VA.
            PMMPTE pte = MiGetPtePointer(VirtualAddress);

            // Write the PTE.
            MI_WRITE_PTE(pte, VirtualAddress, PPFN_TO_PHYSICAL_ADDRESS(INDEX_TO_PPFN(pfn)), PAGE_PRESENT | PAGE_RW);

            // Return success.
            return MT_SUCCESS;
        }

        else {
            goto PageFaultBugCheck;
        }
    }

    // This is checked after KernelVaStart to sanitize valid kernel requests.
    if (VirtualAddress >= USER_VA_END) {
        // User above user VA limit.
        if (PreviousMode == UserMode) {
            return MT_ACCESS_VIOLATION;
        }

        if (MmInvalidAccessAllowed()) {
            return MT_ACCESS_VIOLATION;
        }

        goto PageFaultBugCheck;
    }

    // The address is in the user mode virtual address range.
    if (PreviousMode == UserMode) {
        PMMVAD vad = MiFindVad(PsGetCurrentProcess()->VadRoot, VirtualAddress);
        if (vad) {
            // Map the page, not the whole VAD.
            // Acquire a PFN from physical memory.
            PAGE_INDEX pfn = MiRequestPhysicalPage(PfnStateFree);
            if (pfn == PFN_ERROR) goto PageFaultBugCheck; // TODO OOM

            // Acquire the PTE for the faulty VA.
            PMMPTE pte = MiGetPtePointer(VirtualAddress);

            // Write the PTE.
            MI_WRITE_PTE(pte, VirtualAddress, PPFN_TO_PHYSICAL_ADDRESS(INDEX_TO_PPFN(pfn)), PAGE_PRESENT | PAGE_RW);

            // Return success.
            return MT_SUCCESS;
        }
        else {
            return MT_ACCESS_VIOLATION;
        }
    }

    // The kernel has faulted in a user mode address, check if invalid access is allowed, if not, bugcheck.
    if (MmInvalidAccessAllowed()) {
        return MT_ACCESS_VIOLATION;
    }

    goto PageFaultBugCheck;

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

        This function determines if invalid access (e.g, a null pointer dereference), is allowed within the current context.

    Arguments:

        None.

    Return Values:

        True if invalid access is allowed, false otherwise.

--*/


{
    if (MeGetCurrentIrql() >= DISPATCH_LEVEL) {
        return false;
    }

    if (MeGetPreviousMode() == UserMode) {
        return true;
    }

    // FIXME Check for an exception handler.

    return false; // This should change when we introduce the handler check.
}