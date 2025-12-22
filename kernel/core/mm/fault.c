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
#include "../../includes/mh.h"
#include "../../includes/me.h"
#include "../../includes/ps.h"
#include "../../includes/mg.h"
#include "../../assert.h"
#include "../../includes/fs.h"

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
            MT_SUCCESS -- Fault handled, return.
            MT_ACCESS_VIOLATION -- User mode only (or kernel mode probing).
            MT_GUARD_PAGE_VIOLATION -- Bugchecks.

        The function would bugcheck if an invalid kernel mode access occured (or in worst case, 0 memory is available to fill the VAD of the user mode process, but I want to change it to sleep instead..)

--*/

{
    // Declarations
#ifdef DEBUG
    // These are used when I'm debugging.
    PMMPTE ReferencedPml4e = MiGetPml4ePointer(VirtualAddress);
    PMMPTE ReferencedPdpte = MiGetPdptePointer(VirtualAddress);
    PMMPTE ReferencedPde = MiGetPdePointer(VirtualAddress);
    UNREFERENCED_PARAMETER(ReferencedPml4e); UNREFERENCED_PARAMETER(ReferencedPdpte); UNREFERENCED_PARAMETER(ReferencedPde);
#endif
    PMMPTE ReferencedPte = MiGetPtePointer(VirtualAddress);
    FAULT_OPERATION OperationDone = MiRetrieveOperationFromErrorCode(FaultBits);
    IRQL PreviousIrql = MeGetCurrentIrql();

#ifdef DEBUG
    gop_printf(COLOR_RED, "Inside MmAccessFault | FaultBits: %llx | VirtualAddress: %p | PreviousMode: %d | TrapFrame->rip: %p | Operation: %d | Irql: %d\n", (unsigned long long)FaultBits, (void*)(uintptr_t)VirtualAddress, PreviousMode, (void*)(uintptr_t)TrapFrame->rip, OperationDone, PreviousIrql);
#endif

    if (!ReferencedPte) {
        // If we cannot get the PTE for the VA, we raise access violation if its user mode, or bugcheck on kernel mode.
        if (PreviousMode == UserMode) {
            return MT_ACCESS_VIOLATION;
        }

        // Bugcheck here, operations in the Bugcheck label use the ReferencedPte pointer.
        MeBugCheckEx(
            PAGE_FAULT,
            (void*)VirtualAddress,
            (void*)MiRetrieveOperationFromErrorCode(TrapFrame->error_code),
            (void*)TrapFrame->rip,
            (void*)FaultBits
        );
    }

    // If the VA given isn't canonical (sign extended after bit 47, required by CPU MMU Laws), we return or bugcheck depending on the previous mode.
    if (!MI_IS_CANONICAL_ADDR(VirtualAddress)) {

        if (PreviousMode == UserMode) {
            // User mode fault on non canonical address, not destructive.
            return MT_ACCESS_VIOLATION;
        }

        // Kernel mode page fault on a non canonical address.
        goto BugCheck;

    }

    // Check for NX. (NX on anywhere is invalid, no matter the range)
    if (OperationDone == ExecuteOperation) {
        // Check if the page has NoExecute.
        if (ReferencedPte->Hard.NoExecute) {
            // Execution is disallowed.
            if (PreviousMode == UserMode) {
                // UserMode executions get an access violation.
                return MT_ACCESS_VIOLATION;
            }
            else {
                // KernelMode violations are bugchecks.
                goto BugCheck;
            }
        }

        // The page is executable allowed, we check if we demand allocate (of if it is a VAD for user mode)
        // Previously it returned an access violation for every time we executed wrong in user mode, which was bad.
    }

    // Now we check for each address in the system, and handle the request based on that.
    if (VirtualAddress >= MmSystemRangeStart) {
        if (PreviousMode == UserMode) {
            // User mode access in kernel memory, invalid.
            return MT_ACCESS_VIOLATION;
        }

        MMPTE TempPte = *ReferencedPte;

        // If this is a guard page, we MUST NOT demand allocate it. (pre guard)
        if (TempPte.Hard.Present == 0 && TempPte.Soft.SoftwareFlags & MI_GUARD_PAGE_PROTECTION) {
            goto BugCheck;
        }

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
                MiInvalidateTlbForVa((void*)VirtualAddress);
            }
            return MT_SUCCESS;
        }
        
        // Before any demand allocation, check IRQL.
        if (PreviousIrql >= DISPATCH_LEVEL) {
            // IRQL Isn't less than DISPATCH_LEVEL, so we cannot lazily allocate, since it would **block**.
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
                goto BugCheck;
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
            if (!MiIsValidPfn(pfn)) goto BugCheck;

            // Check the PFN, it has to be in the StandBy list and be equal to our PTE, if not, bugcheck.
            PPFN_ENTRY PPfn = INDEX_TO_PPFN(pfn);
            if (PPfn->State != PfnStateStandby || PPfn->Descriptor.Mapping.PteAddress == NULL || PPfn->Descriptor.Mapping.PteAddress != ReferencedPte) goto BugCheck;

            // PFN Is matching to this pte, now we can allocate, finally.
            // Check protection mask.
            uint64_t ProtectionFlags = PAGE_PRESENT;
            ProtectionFlags |= (TempPte.Soft.SoftwareFlags & PROT_KERNEL_WRITE) ? PAGE_RW : 0;

            MI_WRITE_PTE(ReferencedPte, VirtualAddress, PFN_TO_PHYS(pfn), ProtectionFlags);

            return MT_SUCCESS;
        }

        // TODO GRAB FROM PAGEFILE

        // Unknown PTE format -> bugcheck (kernel space)
        goto BugCheck;
    }

    // Address is below the kernel start, and above user address.
    // This if statement should never pass, since these addresses are non canonical, and the first if statement checks for a non canonical adddres.
    // basically kernel bloat at this point.
    // i removed it, bye bye.

    // Address is in user range.
    // Both kernel and user mode are allowed to fault in here, guranteeing there is a VAD backing it of course (and IRQL demands)
    // If kernel faulted and no vad (Irql is good), then we search for exception handlers in return, if none we bugcheck.
    // If a user faulted and no vad (Irql is good), then we search for exception handlers in return, if none we terminate the thread.
    if (VirtualAddress <= MmHighestUserAddress) {
        // Before any demand allocation, check IRQL.
        if (PreviousIrql >= DISPATCH_LEVEL) {
            // IRQL Isn't less than DISPATCH_LEVEL, so we cannot lazily allocate, since it would **block**.
            MeBugCheckEx(
                IRQL_NOT_LESS_OR_EQUAL,
                (void*)VirtualAddress,
                (void*)PreviousIrql,
                (void*)OperationDone,
                (void*)TrapFrame->rip
            );
        }

        // Fault on a user address, we check if there is a vad for it, if so, allocate the page.
        PMMVAD vad = MiFindVad(PsGetCurrentProcess(), VirtualAddress);
        if (!vad) return MT_ACCESS_VIOLATION; // If kernel mode exception dispatcher should catch.

        // Check if we are allowed to allocate.
        if (vad->Flags & VAD_FLAG_RESERVED) {
            // Allocation is forbidden, return access violation.
            return MT_ACCESS_VIOLATION;
        }

        uint64_t PteFlags = PAGE_PRESENT | PAGE_NX | PAGE_USER;

        // Apply flags.
        if (vad->Flags & VAD_FLAG_WRITE) {
            PteFlags |= PAGE_RW;
        }

        if (vad->Flags & VAD_FLAG_EXECUTE) {
            PteFlags &= ~PAGE_NX;
        }


        // TODO COPY ON WRITE!! (process to process)
        // Looks like we have a valid vad, lets allocate.
        PAGE_INDEX pfn = MiRequestPhysicalPage(PfnStateZeroed);

        if (pfn == PFN_ERROR) return MT_ACCESS_VIOLATION; // TODO OOM

        // Acquire the PTE for the faulty VA.
        PMMPTE pte = MiGetPtePointer(VirtualAddress);

        // Now we check if the VAD has any file attached to it, if it does, we copy the contents of the file to the RAM
        // This could be a process file (executable, dll), or even our pagefile.
        if (vad->File) {
            // Calculate file offset to load into VAD.
            uint64_t AlignedAddress = (uint64_t)PAGE_ALIGN(VirtualAddress);
            uint64_t PageOffsetWithinVad = AlignedAddress - (uint64_t)vad->StartVa;
            uint64_t ActualFileOffset = vad->FileOffset + PageOffsetWithinVad;

            // Determine how many bytes to read from file to the page.
            PFILE_OBJECT FileObject = vad->File;
            uint64_t FileLength = FileObject->FileSize;
            size_t ToRead = 0;

            if (ActualFileOffset < FileLength) {
                ToRead = (size_t)MIN((uint64_t)VirtualPageSize, FileLength - ActualFileOffset);
            }
            else {
                ToRead = 0;
            }

            // Allocate enough buffer size to hold the file.
            void* Tmp = MmAllocatePoolWithTag(NonPagedPool, VirtualPageSize, 'Fpmt'); // tmpF - Temporary Fault
            if (!Tmp) return MT_ACCESS_VIOLATION;

            // Read the file now.
            if (ToRead > 0) {
                MTSTATUS Status = FsReadFile(FileObject, ActualFileOffset, Tmp, ToRead, NULL);
                if (MT_FAILURE(Status)) {
                    MmFreePool(Tmp);
                    return MT_ACCESS_VIOLATION;
                }
            }
            if (ToRead < VirtualPageSize) {
                // zero the rest of the page
                kmemset((uint8_t*)Tmp + ToRead, 0, VirtualPageSize - ToRead);
            }

            // Copy data from the file to the new user Page
            // We must not access the virtual address, as if this is a page without write access, we would fault (like the .text section)
            // Speaking from exprience btw.
            // So we operate on the physical address.
            // This should be IRQL fine, since we filtered dispatch and above, above.
            // As well as performed the read operation in PASSIVE_LEVEL (or APC)
            IRQL oldIrql;
            void* AddressToOperate = MiMapPageInHyperspace(pfn, &oldIrql);
            kmemcpy(AddressToOperate, Tmp, VirtualPageSize);
            MiUnmapHyperSpaceMap(oldIrql);
        }

        // Write the PTE.
        MI_WRITE_PTE(pte, VirtualAddress, PFN_TO_PHYS(pfn), PteFlags);

        // Return success.
        return MT_SUCCESS;
    }

    // Address, is, what... impossible!
    // This comment means execution is impossible to reach here, as we sanitized all (valid) addresses in the 48bit paging hierarchy.
    // If it does reach here, look below.

BugCheck:
    // TODO Check for NX.

    // Check if its a guard page violation
    if (ReferencedPte->Soft.SoftwareFlags & MI_GUARD_PAGE_PROTECTION) {
        MeBugCheckEx(
            GUARD_PAGE_DEREFERENCE,
            (void*)VirtualAddress,
            (void*)MiRetrieveOperationFromErrorCode(TrapFrame->error_code),
            (void*)TrapFrame->rip,
            (void*)FaultBits
        );
    }

    // Check if its a pool dereference (NonPagedPool first)
    if (VirtualAddress >= MmNonPagedPoolStart && VirtualAddress <= MmNonPagedPoolEnd) {
        MeBugCheckEx(
            PAGE_FAULT_IN_FREED_NONPAGED_POOL,
            (void*)VirtualAddress,
            (void*)MiRetrieveOperationFromErrorCode(TrapFrame->error_code),
            (void*)TrapFrame->rip,
            (void*)FaultBits
        );
    }

    // Check if Paged Pool Dereference. (the IRQL_NOT_LESS_OR_EQUAL bugcheck is up top)
    if (VirtualAddress >= MmPagedPoolStart && VirtualAddress <= MmPagedPoolEnd) {
        MeBugCheckEx(
            PAGE_FAULT_IN_FREED_PAGED_POOL,
            (void*)VirtualAddress,
            (void*)MiRetrieveOperationFromErrorCode(TrapFrame->error_code),
            (void*)TrapFrame->rip,
            (void*)FaultBits
        );
    }

    // Normal page fault.
    MeBugCheckEx(
        PAGE_FAULT,
        (void*)VirtualAddress,
        (void*)MiRetrieveOperationFromErrorCode(TrapFrame->error_code),
        (void*)TrapFrame->rip,
        (void*)FaultBits
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