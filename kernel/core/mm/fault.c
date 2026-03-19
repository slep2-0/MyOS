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

    // Grab the TempPte early
    MMPTE TempPte = *ReferencedPte;

    // Handle Present pages globally first (Dirty / Accessed bit updates)
    if (TempPte.Hard.Present) {
        if (OperationDone == WriteOperation) {
            // Check if the page actually has write perms.
            if (TempPte.Hard.Write == 0) {
                if (PreviousMode == UserMode) return MT_ACCESS_VIOLATION;
                MeBugCheckEx(ATTEMPTED_WRITE_TO_READONLY_MEMORY, (void*)VirtualAddress, (void*)ReferencedPte, NULL, NULL);
            }

            // It has write permission, so this is just a dirty bit update.
            MMPTE NewPte = TempPte;
            NewPte.Hard.Dirty = 1;
            MiAtomicExchangePte(ReferencedPte, NewPte.Value);
            MiInvalidateTlbForVa((void*)VirtualAddress);
            return MT_SUCCESS;
        }

        // If it was a ReadOperation on a present page, it was likely an accessed bit update.
        // We handle setting the accessed bit here (TODO Working set, analytics, yada yada)
        return MT_SUCCESS;
    }

    // Now we check for each address in the system, and handle the request based on that.
    if (VirtualAddress >= MmSystemRangeStart) {
        if (PreviousMode == UserMode) {
            // User mode access in kernel memory, invalid.
            return MT_ACCESS_VIOLATION;
        }

        // If this is a guard page, we MUST NOT demand allocate it. (pre guard)
        if (TempPte.Hard.Present == 0 && TempPte.Soft.SoftwareFlags & MI_GUARD_PAGE_PROTECTION) {
            // Guard pages for kernel mode do not raise an exception and fill in the PTE, this is only for user mode.
            goto BugCheck;
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

            // The page first of all must be a protection with readable.
            assert((TempPte.Soft.SoftwareFlags & PROT_KERNEL_READ) == 1, "Read protection flag isnt set on a DEMAND_ZERO pte.");
            
            if ((TempPte.Soft.SoftwareFlags & PROT_KERNEL_READ) == 0) {
                // Invalid DemandZero.
                goto BugCheck;
            }

            // Check protection mask.
            uint64_t ProtectionFlags = PAGE_PRESENT;
            ProtectionFlags |= (TempPte.Soft.SoftwareFlags & PROT_KERNEL_WRITE) ? PAGE_RW : 0;
            ProtectionFlags |= (TempPte.Soft.SoftwareFlags & PROT_KERNEL_NOEXECUTE) ? PAGE_NX : 0;

            // Write the PTE.
            MI_WRITE_PTE(ReferencedPte, VirtualAddress, PPFN_TO_PHYSICAL_ADDRESS(INDEX_TO_PPFN(pfn)), ProtectionFlags);

            return MT_SUCCESS;
        }

        // PTE Isn't present, and its a transition (KERNEL MODE PATH)
        if (TempPte.Soft.Transition == 1) {
            // Retrieve the PFN Number written in the transition page.
            PAGE_INDEX pfn = TempPte.Soft.PageFrameNumber;
            if (!MiIsValidPfn(pfn)) goto BugCheck;

            // Acquire Standby PFN DB List lock. (acquiring spinlock is okay, IRQL detection was checked above)
            IRQL oldIrql;
            MsAcquireSpinlock(&PfnDatabase.StandbyPageList.PfnListLock, &oldIrql);

            // Check the PFN, it has to be in the StandBy list and be equal to our PTE, if not, bugcheck.
            PPFN_ENTRY PPfn = INDEX_TO_PPFN(pfn);
            if (PPfn->State != PfnStateStandby || PPfn->Descriptor.Mapping.PteAddress == NULL || PPfn->Descriptor.Mapping.PteAddress != ReferencedPte) {
                // Release spinlock.
                MsReleaseSpinlock(&PfnDatabase.StandbyPageList.PfnListLock, oldIrql);
                goto BugCheck;
            }
            // PFN Is matching to this pte, now we can set the PTE.
            // Check protection mask.
            uint64_t ProtectionFlags = PAGE_PRESENT;
            ProtectionFlags |= (TempPte.Soft.SoftwareFlags & PROT_KERNEL_WRITE) ? PAGE_RW : 0;
            ProtectionFlags |= (TempPte.Soft.SoftwareFlags & PROT_KERNEL_NOEXECUTE) ? PAGE_NX : 0;

            // Release this PFN from the list.
            MiUnlinkPageFromList(PPfn);

            // Atomically set PTE.
            MI_WRITE_PTE(ReferencedPte, VirtualAddress, PFN_TO_PHYS(pfn), ProtectionFlags);

            // Release PFN Standby list lock.
            MsReleaseSpinlock(&PfnDatabase.StandbyPageList.PfnListLock, oldIrql);

            // Return success.
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
    // Both kernel and user mode are allowed to fault in here, guaranteeing there is a VAD backing it of course (and IRQL demands)
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
            // Check if this is a guard page.
            if (vad->Flags & VAD_FLAG_GUARD_PAGE) {
                // Raise an guard page violation status and allocate the page.
                //ExpRaiseStatus() TODO
                vad->Flags = VAD_FLAG_WRITE | VAD_FLAG_READ;
            }

            else {
                // Allocation is forbidden, return access violation.
                return MT_ACCESS_VIOLATION;
            }
        }

        // Now check for transition PTE (after checking reserved vad flag)
        // PTE Isn't present, and its a transition (USER MODE PATH) (ACCESS VIOLATION RETURN)
        // If the previous mode is kernel mode and an access violation is returned, KMODE_EXCEPTION_NOT_HANDLED bugcheck comes
        // unless the kernel has a try except handler set in the VA (checked in return path)
        if (TempPte.Soft.Transition == 1) {
            // Retrieve the PFN Number written in the transition page.
            PAGE_INDEX pfn = TempPte.Soft.PageFrameNumber;
            if (!MiIsValidPfn(pfn)) return MT_ACCESS_VIOLATION;

            // Acquire Standby PFN DB List lock. (acquiring spinlock is okay, IRQL detection was checked above)
            IRQL oldIrql;
            MsAcquireSpinlock(&PfnDatabase.StandbyPageList.PfnListLock, &oldIrql);

            // Check the PFN, it has to be in the StandBy list and be equal to our PTE, if not, bugcheck.
            PPFN_ENTRY PPfn = INDEX_TO_PPFN(pfn);
            if (PPfn->State != PfnStateStandby || PPfn->Descriptor.Mapping.PteAddress == NULL || PPfn->Descriptor.Mapping.PteAddress != ReferencedPte) {
                // Release spinlock.
                MsReleaseSpinlock(&PfnDatabase.StandbyPageList.PfnListLock, oldIrql);
                return MT_ACCESS_VIOLATION;
            }

            // PFN Is matching to this pte, now we can set the PTE.
            // Check protection mask.
            uint64_t ProtectionFlags = PAGE_PRESENT;

            // Writable
            ProtectionFlags |= (TempPte.Soft.SoftwareFlags & PROT_KERNEL_WRITE) ? PAGE_RW : 0;
            
            // User accessible.
            assert((TempPte.Soft.SoftwareFlags & PROT_KERNEL_USER) != 0);
            ProtectionFlags |= (TempPte.Soft.SoftwareFlags & PROT_KERNEL_USER) ? PAGE_USER : 0; // This should always be valid for user mode paths.
            
            // NoExecute.
            ProtectionFlags |= (TempPte.Soft.SoftwareFlags & PROT_KERNEL_NOEXECUTE) ? PAGE_NX : 0;

            // Release this PFN from the list.
            MiUnlinkPageFromList(PPfn);

            // Atomically set PTE.
            MI_WRITE_PTE(ReferencedPte, VirtualAddress, PFN_TO_PHYS(pfn), ProtectionFlags);

            // Release PFN Standby list lock.
            MsReleaseSpinlock(&PfnDatabase.StandbyPageList.PfnListLock, oldIrql);

            // Return success.
            return MT_SUCCESS;
        }

        // Set to base values.
        uint64_t PteFlags = PAGE_PRESENT | PAGE_NX | PAGE_USER;

        // Apply flags.
        if (vad->Flags & VAD_FLAG_WRITE) {
            PteFlags |= PAGE_RW;
        }

        if (vad->Flags & VAD_FLAG_EXECUTE) {
            PteFlags &= ~PAGE_NX;
        }


        // TODO COPY ON WRITE!!
        /*
        if (vad->Flags & VAD_FLAG_COPY_ON_WRITE) {
            // Copy the physical address to the COW page.
        }
        */
        
        // Looks like we have a valid vad, lets allocate.
        PAGE_INDEX pfn = MiRequestPhysicalPage(PfnStateZeroed);

        if (pfn == PFN_ERROR) return MT_ACCESS_VIOLATION;

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

            // NOTE: Is this really needed? Pool allocations are zeroed, and we used a PfnStateZeroed phys page up top.
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
    // Bugchecks for: IRQL_NOT_LESS_OR_EQUAL or ATTEMPTED_WRITE_TO_READONLY_MEMORY are handled above.

    // Check if its a NoExecute page violation
    if (ReferencedPte->Hard.Present && ReferencedPte->Hard.NoExecute && OperationDone == ExecuteOperation) {
        MeBugCheckEx(
            ATTEMPTED_EXECUTE_OF_NOEXECUTE_MEMORY,
            (void*)VirtualAddress,
            (void*)MiRetrieveOperationFromErrorCode(TrapFrame->error_code),
            (void*)TrapFrame->rip,
            (void*)FaultBits
        );
    }
    
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