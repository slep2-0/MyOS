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

static bool
MipVadAllowsAccess(
    IN VAD_FLAGS Flags,
    IN FAULT_OPERATION Operation
)
{
    if (Operation == WriteOperation) {
        return (Flags & VAD_FLAG_WRITE) != 0;
    }

    if (Operation == ExecuteOperation) {
        return (Flags & VAD_FLAG_EXECUTE) != 0;
    }

    return (Flags & VAD_FLAG_READ) != 0;
}

static uint64_t
MipBuildUserPteFlags(
    IN VAD_FLAGS Flags
)
{
    uint64_t PteFlags = PAGE_PRESENT | PAGE_USER | PAGE_NX;

    if (Flags & VAD_FLAG_WRITE) {
        PteFlags |= PAGE_RW;
    }

    if (Flags & VAD_FLAG_EXECUTE) {
        PteFlags &= ~PAGE_NX;
    }

    return PteFlags;
}

static bool
MipCalculateFileOffset(
    IN PMMVAD Vad,
    IN uintptr_t VirtualAddress,
    OUT uint64_t* FileOffset
)
{
    uint64_t AlignedAddress = (uint64_t)PAGE_ALIGN(VirtualAddress);
    uint64_t StartAddress = (uint64_t)Vad->StartVa;

    if (AlignedAddress < StartAddress) return false;

    uint64_t RelativeOffset = AlignedAddress - StartAddress;
    if (Vad->FileOffset > UINT64_MAX - RelativeOffset) return false;

    *FileOffset = Vad->FileOffset + RelativeOffset;
    return true;
}

static
bool
MipPublishPage(
    IN PMMPTE Pte,
    IN uintptr_t VirtualAddress,
    IN PAGE_INDEX PfnIndex,
    IN uint64_t PteFlags,
    IN uint64_t ExpectedPte,
    IN uint32_t PfnFlags,
    _In_Opt PMMVAD Vad
)
{
    uint64_t NewPte = PFN_TO_PHYS(PfnIndex) | PteFlags;
    PPFN_ENTRY Pfn = INDEX_TO_PPFN(PfnIndex);

    // A present PTE may immediately be observed and torn down by another CPU.
    // Initialize its reverse map before publication so teardown never sees the
    // stale availability-list overlay in Descriptor.
    Pfn->Descriptor.Mapping.PteAddress = Pte;
    Pfn->Descriptor.Mapping.Vad = Vad;
    Pfn->State = PfnStateActive;
    Pfn->Flags = PfnFlags;

    if (!MiAtomicSetPte(Pte, NewPte, ExpectedPte)) {
        // This PFN never became visible. Restore the private claimed state so
        // the caller can release it without treating the winning PTE as ours.
        Pfn->Descriptor.Mapping.PteAddress = NULL;
        Pfn->Descriptor.Mapping.Vad = NULL;
        Pfn->State = PfnStateTransition;
        Pfn->Flags = PFN_FLAG_NONE;
        return false;
    }

    MiInvalidateTlbForVa((void*)VirtualAddress);
    return true;
}

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
    // Page-table helpers only accept canonical addresses. Validate CR2 before
    // constructing any recursive-map pointer from it.
    if (!MI_IS_CANONICAL_ADDR(VirtualAddress)) {
        if (PreviousMode == UserMode) {
            return MT_ACCESS_VIOLATION;
        }

        MeBugCheckEx(PAGE_FAULT, (void*)VirtualAddress,
            (void*)MiRetrieveOperationFromErrorCode(TrapFrame->error_code),
            (void*)TrapFrame->rip, (void*)FaultBits);
    }

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

    // Grab the TempPte early
    MMPTE TempPte = *ReferencedPte;

    // If the CPU reported P=1, this was a protection violation (RW, US, NX,
    // SMEP, SMAP, PKU, and so on). x86 updates Accessed/Dirty in hardware and
    // does not fault merely to ask software to set those bits.
    if (FaultBits & PAGE_PRESENT) {
        return MT_ACCESS_VIOLATION;
    }

    // Another CPU may have resolved the not-present fault before we sampled
    // the PTE. In that one case retrying the instruction is correct.
    if (TempPte.Hard.Present) {
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
                MiReleasePhysicalPage(pfn);
                goto BugCheck;
            }

            // Check protection mask.
            uint64_t ProtectionFlags = PAGE_PRESENT;
            ProtectionFlags |= (TempPte.Soft.SoftwareFlags & PROT_KERNEL_WRITE) ? PAGE_RW : 0;
            ProtectionFlags |= (TempPte.Soft.SoftwareFlags & PROT_KERNEL_NOEXECUTE) ? PAGE_NX : 0;

            if (!MipPublishPage(
                ReferencedPte,
                VirtualAddress,
                pfn,
                ProtectionFlags,
                TempPte.Value,
                PFN_FLAG_NONPAGED,
                NULL
            )) {
                MiReleasePhysicalPage(pfn);

                MMPTE ObservedPte = *ReferencedPte;
                if (!ObservedPte.Hard.Present &&
                    !ObservedPte.Soft.Transition) {
                    goto BugCheck;
                }
            }

            // Either we published it or another CPU resolved/transitioned it.
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

            MMPTE CurrentPte = *ReferencedPte;
            if (CurrentPte.Hard.Present) {
                MsReleaseSpinlock(&PfnDatabase.StandbyPageList.PfnListLock, oldIrql);
                return MT_SUCCESS;
            }

            if (!CurrentPte.Soft.Transition ||
                CurrentPte.Soft.PageFrameNumber != pfn) {
                MsReleaseSpinlock(&PfnDatabase.StandbyPageList.PfnListLock, oldIrql);
                goto BugCheck;
            }

            // A list-linked PFN uses Descriptor.ListEntry, so Mapping cannot
            // simultaneously contain a PTE pointer. The transition PTE is the
            // authoritative owner until the page is activated.
            PPFN_ENTRY PPfn = INDEX_TO_PPFN(pfn);
            if (PPfn->State != PfnStateStandby || PPfn->RefCount != 0) {
                // Release spinlock.
                MsReleaseSpinlock(&PfnDatabase.StandbyPageList.PfnListLock, oldIrql);
                goto BugCheck;
            }
            // PFN Is matching to this pte, now we can set the PTE.
            // Check protection mask.
            uint64_t ProtectionFlags = PAGE_PRESENT;
            ProtectionFlags |= (CurrentPte.Soft.SoftwareFlags & PROT_KERNEL_WRITE) ? PAGE_RW : 0;
            ProtectionFlags |= (CurrentPte.Soft.SoftwareFlags & PROT_KERNEL_NOEXECUTE) ? PAGE_NX : 0;

            RemoveEntryList(&PPfn->Descriptor.ListEntry);
            PPfn->Descriptor.ListEntry.Flink = NULL;
            PPfn->Descriptor.ListEntry.Blink = NULL;
            InterlockedDecrementU64(&PfnDatabase.StandbyPageList.Count);
            InterlockedDecrementU64(&PfnDatabase.AvailablePages);
            PPfn->RefCount = 1;
            PPfn->State = PfnStateTransition;

            if (!MipPublishPage(
                ReferencedPte,
                VirtualAddress,
                pfn,
                ProtectionFlags,
                CurrentPte.Value,
                PFN_FLAG_NONPAGED,
                NULL
            )) {
                MsReleaseSpinlock(&PfnDatabase.StandbyPageList.PfnListLock, oldIrql);
                MeBugCheckEx(PFN_TRANSITION_FAILURE, ReferencedPte,
                    (void*)(uintptr_t)pfn, (void*)CurrentPte.Value,
                    (void*)(uintptr_t)ReferencedPte->Value);
            }

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

        PEPROCESS Process = PsGetCurrentProcess();
        PFILE_OBJECT FileObject = NULL;
        uint64_t ActualFileOffset = 0;

        // VAD mutation and transition-page activation are short operations and
        // stay serialized. Slow file I/O happens after this lock is released.
        MsAcquirePushLockExclusive(&Process->VadLock);
        PMMVAD Vad = MiFindVadInternal(Process, VirtualAddress, false);
        if (!Vad) {
            MsReleasePushLockExclusive(&Process->VadLock);
            return MT_ACCESS_VIOLATION;
        }

        if (Vad->Flags & VAD_FLAG_RESERVED) {
            if (!(Vad->Flags & VAD_FLAG_GUARD_PAGE)) {
                MsReleasePushLockExclusive(&Process->VadLock);
                return MT_ACCESS_VIOLATION;
            }

            Vad->Flags &= ~(VAD_FLAG_RESERVED | VAD_FLAG_GUARD_PAGE);
            Vad->Flags |= VAD_FLAG_READ | VAD_FLAG_WRITE;
        }

        if (!MipVadAllowsAccess(Vad->Flags, OperationDone)) {
            MsReleasePushLockExclusive(&Process->VadLock);
            return MT_ACCESS_VIOLATION;
        }

        MMPTE UserPte = *ReferencedPte;
        if (UserPte.Hard.Present) {
            MsReleasePushLockExclusive(&Process->VadLock);
            return MT_SUCCESS;
        }

        if (UserPte.Soft.Transition) {
            PAGE_INDEX PfnIndex = UserPte.Soft.PageFrameNumber;
            if (!MiIsValidPfn(PfnIndex)) {
                MsReleasePushLockExclusive(&Process->VadLock);
                return MT_ACCESS_VIOLATION;
            }

            PPFN_ENTRY Pfn = INDEX_TO_PPFN(PfnIndex);
            IRQL OldIrql;
            MsAcquireSpinlock(
                &PfnDatabase.StandbyPageList.PfnListLock,
                &OldIrql
            );

            if (Pfn->State != PfnStateStandby || Pfn->RefCount != 0) {
                MsReleaseSpinlock(
                    &PfnDatabase.StandbyPageList.PfnListLock,
                    OldIrql
                );
                MsReleasePushLockExclusive(&Process->VadLock);
                return MT_ACCESS_VIOLATION;
            }

            RemoveEntryList(&Pfn->Descriptor.ListEntry);
            Pfn->Descriptor.ListEntry.Flink = NULL;
            Pfn->Descriptor.ListEntry.Blink = NULL;
            InterlockedDecrementU64(&PfnDatabase.StandbyPageList.Count);
            InterlockedDecrementU64(&PfnDatabase.AvailablePages);
            Pfn->RefCount = 1;
            Pfn->State = PfnStateTransition;

            MsReleaseSpinlock(
                &PfnDatabase.StandbyPageList.PfnListLock,
                OldIrql
            );

            if (!MipPublishPage(
                ReferencedPte,
                VirtualAddress,
                PfnIndex,
                MipBuildUserPteFlags(Vad->Flags),
                UserPte.Value,
                PFN_FLAG_NONE,
                Vad
            )) {
                MsReleasePushLockExclusive(&Process->VadLock);
                MeBugCheckEx(PFN_TRANSITION_FAILURE, ReferencedPte,
                    (void*)(uintptr_t)PfnIndex, (void*)UserPte.Value,
                    (void*)(uintptr_t)ReferencedPte->Value);
            }
            MsReleasePushLockExclusive(&Process->VadLock);
            return MT_SUCCESS;
        }

        // These formats do not have a resolver yet. Treating either one as a
        // demand-zero PTE would discard its backing-store information.
        if (UserPte.Soft.Prototype || UserPte.Soft.PageFile) {
            MsReleasePushLockExclusive(&Process->VadLock);
            return MT_ACCESS_VIOLATION;
        }

        if (Vad->File) {
            if (!MipCalculateFileOffset(
                Vad,
                VirtualAddress,
                &ActualFileOffset
            )) {
                MsReleasePushLockExclusive(&Process->VadLock);
                return MT_ACCESS_VIOLATION;
            }

            FileObject = Vad->File;
            if (!ObReferenceObject(FileObject)) {
                MsReleasePushLockExclusive(&Process->VadLock);
                return MT_ACCESS_VIOLATION;
            }
        }

        MsReleasePushLockExclusive(&Process->VadLock);

        PAGE_INDEX PfnIndex = MiRequestPhysicalPage(PfnStateZeroed);
        if (PfnIndex == PFN_ERROR) {
            if (FileObject) ObDereferenceObject(FileObject);
            return MT_ACCESS_VIOLATION;
        }

        if (FileObject) {
            size_t ToRead = 0;
            if (ActualFileOffset < FileObject->FileSize) {
                ToRead = (size_t)MIN(
                    (uint64_t)VirtualPageSize,
                    FileObject->FileSize - ActualFileOffset
                );
            }

            void* TemporaryBuffer = MmAllocatePoolWithTag(
                NonPagedPool,
                VirtualPageSize,
                'Fpmt'
            );
            if (!TemporaryBuffer) {
                MiReleasePhysicalPage(PfnIndex);
                ObDereferenceObject(FileObject);
                return MT_ACCESS_VIOLATION;
            }

            MTSTATUS ReadStatus = MT_SUCCESS;
            if (ToRead != 0) {
                ReadStatus = FsReadFile(
                    FileObject,
                    ActualFileOffset,
                    TemporaryBuffer,
                    ToRead,
                    NULL
                );
            }

            if (MT_FAILURE(ReadStatus)) {
                MmFreePool(TemporaryBuffer);
                MiReleasePhysicalPage(PfnIndex);
                ObDereferenceObject(FileObject);
                return MT_ACCESS_VIOLATION;
            }

            if (ToRead < VirtualPageSize) {
                kmemset(
                    (uint8_t*)TemporaryBuffer + ToRead,
                    0,
                    VirtualPageSize - ToRead
                );
            }

            IRQL OldIrql;
            void* HyperAddress = MiMapPageInHyperspace(PfnIndex, &OldIrql);
            kmemcpy(HyperAddress, TemporaryBuffer, VirtualPageSize);
            MiUnmapHyperSpaceMap(OldIrql);
            MmFreePool(TemporaryBuffer);
        }

        // The VAD may have been protected, split, or freed during file I/O.
        // Revalidate both ownership and file offset before publishing the PTE.
        MsAcquirePushLockExclusive(&Process->VadLock);
        Vad = MiFindVadInternal(Process, VirtualAddress, false);
        if (!Vad || (Vad->Flags & VAD_FLAG_RESERVED) ||
            !MipVadAllowsAccess(Vad->Flags, OperationDone)) {
            MsReleasePushLockExclusive(&Process->VadLock);
            MiReleasePhysicalPage(PfnIndex);
            if (FileObject) ObDereferenceObject(FileObject);
            return MT_ACCESS_VIOLATION;
        }

        bool MappingChanged = Vad->File != FileObject;
        if (!MappingChanged && FileObject) {
            uint64_t CurrentFileOffset;
            MappingChanged = !MipCalculateFileOffset(
                Vad,
                VirtualAddress,
                &CurrentFileOffset
            ) || CurrentFileOffset != ActualFileOffset;
        }

        UserPte = *ReferencedPte;
        if (MappingChanged || UserPte.Soft.Transition) {
            MsReleasePushLockExclusive(&Process->VadLock);
            MiReleasePhysicalPage(PfnIndex);
            if (FileObject) ObDereferenceObject(FileObject);
            // The next fault revalidates the replacement mapping or activates
            // the transition page; our freshly read page no longer applies.
            return MT_SUCCESS;
        }


        if (UserPte.Soft.Prototype || UserPte.Soft.PageFile) {
            MsReleasePushLockExclusive(&Process->VadLock);
            MiReleasePhysicalPage(PfnIndex);
            if (FileObject) ObDereferenceObject(FileObject);
            return MT_ACCESS_VIOLATION;
        }

        if (UserPte.Hard.Present) {
            MsReleasePushLockExclusive(&Process->VadLock);
            MiReleasePhysicalPage(PfnIndex);
            if (FileObject) ObDereferenceObject(FileObject);
            return MT_SUCCESS;
        }

        if (!MipPublishPage(
            ReferencedPte,
            VirtualAddress,
            PfnIndex,
            MipBuildUserPteFlags(Vad->Flags),
            UserPte.Value,
            FileObject ? PFN_FLAG_MAPPED_FILE : PFN_FLAG_NONE,
            Vad
        )) {
            MMPTE ObservedPte = *ReferencedPte;
            bool FaultWasResolved = ObservedPte.Hard.Present ||
                ObservedPte.Soft.Transition;
            MsReleasePushLockExclusive(&Process->VadLock);
            MiReleasePhysicalPage(PfnIndex);
            if (FileObject) ObDereferenceObject(FileObject);
            return FaultWasResolved ? MT_SUCCESS : MT_ACCESS_VIOLATION;
        }
        MsReleasePushLockExclusive(&Process->VadLock);

        if (FileObject) ObDereferenceObject(FileObject);
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
