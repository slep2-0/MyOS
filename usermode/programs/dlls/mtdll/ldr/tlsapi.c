/*++

Module Name:

    tlsapi.c

Purpose:

    This translation unit contains the implementation of initializing thread local storage (TLS) on a module.

Author:

    slep (Matanel) 2026.

Revision History:

--*/

#include "../includes/mtdll.h"
#include "../includes/errorhandlingapi.h"
#include "../includes/ioapi.h"
#include "mte.h"

static
bool
LdrpIsImageRangeValid(
    IN uint64_t ImageSize,
    IN uint64_t Rva,
    IN uint64_t Size
)
{
    if (Rva > ImageSize) {
        return false;
    }

    return Size <= ImageSize - Rva;
}

MTSTATUS
LdrRegisterModuleTlsLocked(
    IN PPEB Peb,
    IN OUT PLDR_DATA_TABLE_ENTRY Module
)

{
    // Reject NULL Modules.
    if (!Peb || !Module || !Module->Base) return MT_INVALID_PARAM;

    // The MTE_HEADER must be a valid size.
    if (sizeof(MTE_HEADER) > Module->SizeOfImage) {
        return MT_INVALID_IMAGE_FORMAT;
    }

    // A module must not be TLS-initialized already
    if (Module->TlsIndex != MT_INVALID_TLS_INDEX) {
        return MT_INVALID_STATE;
    }

    // Read the MTE Header.
    PMTE_HEADER Header = (PMTE_HEADER)Module->Base;

    // Validate the correct MTE Header.
    uint8_t* TempHeader = (uint8_t*)Header;
    if (TempHeader[0] != 'M' || TempHeader[1] != 'T' || TempHeader[2] != 'E' || TempHeader[3] != '\0') {
        return MT_INVALID_IMAGE_FORMAT;
    }

    // If the Module has no TLS, then return success.
    if (Header->tls_rva == 0 && Header->tls_size == 0) return MT_SUCCESS;

    // Check for a malformed image
    if (Header->tls_rva == 0 || Header->tls_size == 0) {
        // If one of the values is 0, then the image is malformed
        return MT_INVALID_IMAGE_FORMAT;
    }

    // Require a valid stable header TLS Size
    if (Header->tls_size != sizeof(MTE_TLS_DIRECTORY)) return MT_INVALID_IMAGE_FORMAT;

    // Validate Base + RVA wont overflow AND is inside the header
    if (!LdrpIsImageRangeValid(Module->SizeOfImage, Header->tls_rva, Header->tls_size)) return MT_INVALID_IMAGE_FORMAT;

    // Finally copy the struct over, it will be copied again
    MTE_TLS_DIRECTORY Directory = *(PMTE_TLS_DIRECTORY)((uintptr_t)Module->Base + Header->tls_rva);

    // Validate the directory itself now
    // It must be nonzero
    // Sizes must not be higher than the total size
    // Alignment must be nonzero and a power of 2.
    if (Directory.total_size == 0 ||
        Directory.template_size > Directory.total_size ||
        Directory.alignment == 0 ||
        (Directory.alignment & (Directory.alignment - 1)) != 0) {
        return MT_INVALID_IMAGE_FORMAT;
    }

    // Validate the template RVA and SIZE are in the module image.
    if (!LdrpIsImageRangeValid(
        Module->SizeOfImage,
        Directory.template_rva,
        Directory.template_size
    ) ||
        !LdrpIsImageRangeValid(
            Module->SizeOfImage,
            Directory.template_rva,
            Directory.total_size
        )) {
        return MT_INVALID_IMAGE_FORMAT;
    }

    // Validate the fixup array now
    bool HasFixupRva = Directory.module_index_fixups_rva != 0;
    bool HasFixupSize = Directory.module_index_fixups_size != 0;

    if (HasFixupRva != HasFixupSize) {
        // Malformed image
        return MT_INVALID_IMAGE_FORMAT;
    }

    // Make sure we arent working with partial records
    if (Directory.module_index_fixups_size % sizeof(MTE_TLS_MODULE_INDEX_FIXUP) != 0) {
        return MT_INVALID_IMAGE_FORMAT;
    }

    // Validate the array fixup lives inside the image
    if (!LdrpIsImageRangeValid(Module->SizeOfImage, Directory.module_index_fixups_rva, Directory.module_index_fixups_size)) {
        return MT_INVALID_IMAGE_FORMAT;
    }

    // Validate all fixup targets before applying them
    PMTE_TLS_MODULE_INDEX_FIXUP Fixups = (PMTE_TLS_MODULE_INDEX_FIXUP)((uint8_t*)Module->Base + Directory.module_index_fixups_rva);
    size_t FixupCount = Directory.module_index_fixups_size / sizeof(MTE_TLS_MODULE_INDEX_FIXUP);

    // Validate TLS fixup targets
    for (size_t i = 0; i < FixupCount; i++) {
        if (!LdrpIsImageRangeValid(Module->SizeOfImage, Fixups[i].target_rva, sizeof(uint64_t))) {
            // Each module index fixup must have a complete 8 byte range inside of the module
            return MT_INVALID_IMAGE_FORMAT;
        }

        uint64_t Target;
        memcpy(&Target, (const void*)((uint8_t*)Module->Base + Fixups[i].target_rva), sizeof(Target));

        if (Target != 0) {
            // The packer gurantees the target destination at each module index is 0
            // nonzero means the image has been applied a fixup already, or is just malformed
            return MT_INVALID_IMAGE_FORMAT;
        }
    }

    if (Peb->NextTlsIndex == MT_INVALID_TLS_INDEX) {
        // Hit the UINT64 limit, but im guessing the system expired out of
        // comitted memory way before we reached here lol
        return MT_NO_RESOURCES;
    }

    // Save the index
    uint64_t AssignedIndex = Peb->NextTlsIndex++;

    // Apply TLS Fixups
    for (size_t i = 0; i < FixupCount; i++) {
        void* Target = (void*)((uint8_t*)Module->Base + Fixups[i].target_rva);
        memcpy(Target, &AssignedIndex, sizeof(AssignedIndex));
    }

    // Finally publish the validated data into the module
    Module->TlsDirectory = Directory;
    Module->TlsIndex = AssignedIndex;
    return MT_SUCCESS;
}

static
MTSTATUS
LdrpAllocateModuleTlsBlock(
    IN PLDR_DATA_TABLE_ENTRY Module,
    IN bool IsExecutable,
    OUT void** TlsBlock,
    OUT void** RawAllocation,
    OUT void** ThreadPointer
)

{
    if (!Module || !TlsBlock || !RawAllocation || !ThreadPointer) return MT_INVALID_PARAM;

    // Initialize outputs
    *TlsBlock = NULL;
    *RawAllocation = NULL;
    *ThreadPointer = NULL;

    size_t TotalSize = Module->TlsDirectory.total_size;
    size_t Alignment = Module->TlsDirectory.alignment;

    if (Alignment == 0 ||
        (Alignment & (Alignment - 1)) != 0) {
        return MT_INVALID_IMAGE_FORMAT;
    }

    PMTE_HEADER Header = (PMTE_HEADER)Module->Base;
    if (Header->PreferredImageBase >
        UINT64_MAX - Module->TlsDirectory.template_rva) {
        return MT_INVALID_IMAGE_FORMAT;
    }

    // Preserve the linked PT_TLS address within its alignment boundary.
    uint64_t TemplateVirtualAddress =
        Header->PreferredImageBase +
        Module->TlsDirectory.template_rva;
    size_t TemplateBias =
        (size_t)(TemplateVirtualAddress & (Alignment - 1));

    if (TemplateBias > SIZE_MAX - TotalSize) {
        return MT_NO_RESOURCES;
    }

    size_t TlsSpan = TemplateBias + TotalSize;
    if (TlsSpan > SIZE_MAX - (Alignment - 1)) {
        return MT_NO_RESOURCES;
    }

    TlsSpan =
        (TlsSpan + Alignment - 1) &
        ~(Alignment - 1);

    // Prevent overflow once again
    if (Alignment - 1 > SIZE_MAX - sizeof(void*)) {
        return MT_NO_RESOURCES;
    }

    size_t PrefixSize = sizeof(void*) + Alignment - 1;
    size_t SuffixSize = IsExecutable ? sizeof(void*) : 0;

    if (PrefixSize > SIZE_MAX - TlsSpan) {
        return MT_NO_RESOURCES;
    }

    size_t AllocationSize = PrefixSize + TlsSpan;

    if (AllocationSize > SIZE_MAX - SuffixSize) {
        return MT_NO_RESOURCES;
    }

    AllocationSize += SuffixSize;

    void* Allocation = HeapAlloc(GetProcessHeap(), HEAP_ALLOCATE_ZERO_MEMORY, AllocationSize);
    if (!Allocation) return GetLastStatus();

    // Align the block near the rwa pointer area
    uintptr_t Candidate =
        (uintptr_t)Allocation + sizeof(void*);

    uintptr_t RegionBase =
        (Candidate + Alignment - 1) &
        ~(uintptr_t)(Alignment - 1);

    void* Block = (void*)(RegionBase + TemplateBias);

    // Save the real allocation before the block
    memcpy(
        (uint8_t*)Block - sizeof(Allocation),
        &Allocation,
        sizeof(Allocation)
    );

    // Copy the module's initialized TLS template
    memcpy(
        Block,
        (uint8_t*)Module->Base +
        Module->TlsDirectory.template_rva,
        (size_t)Module->TlsDirectory.template_size
    );

    void* NewThreadPointer = NULL;

    if (IsExecutable) {
        NewThreadPointer = (void*)(RegionBase + TlsSpan);

        // Clang reads FS:[0] to obtain the thread's pointer
        *(void**)NewThreadPointer = NewThreadPointer;
    }

    // Set the new initialized variables then return success.
    *TlsBlock = Block;
    *RawAllocation = Allocation;
    *ThreadPointer = NewThreadPointer;

    return MT_SUCCESS;
}

static
MTSTATUS
LdrpFreeModuleTlsBlock(
    IN void* TlsBlock
)

/*++

    Routine description:

        Releases the heap allocation that owns one thread-local storage block.

    Arguments:

        [IN] TlsBlock - Module TLS block whose backing allocation is released.

    Return Values:

        MT_SUCCESS when the block is released, MT_INVALID_STATE when its saved
        allocation is missing, or the current heap failure status.

    Notes:

        The TLS allocator stores the original heap pointer immediately before
        the aligned block.

--*/

{
    if (!TlsBlock) return MT_SUCCESS;

    void* Allocation = NULL;
    memcpy(
        &Allocation,
        (uint8_t*)TlsBlock - sizeof(Allocation),
        sizeof(Allocation)
    );

    if (!Allocation) return MT_INVALID_STATE;

    if (!HeapFree(
            GetProcessHeap(),
            HEAP_FREE_NO_OPTIONS,
            Allocation
        )) {
        return GetLastStatus();
    }

    return MT_SUCCESS;
}

MTSTATUS
LdrpInitializeThreadTls(
    IN OUT PTEB Teb,
    IN PPEB Peb
)

{
    if (!Teb || !Peb) return MT_INVALID_PARAM;

    // Don't initialize the same TEB twice.
    if (Teb->TlsSlots ||
        Teb->TlsSlotCount != 0 ||
        Teb->StaticTlsAllocation ||
        Teb->ThreadPointer) {
        return MT_INVALID_STATE;
    }

    uint32_t WaitResult = WaitForSingleObject(Peb->LoaderData.LoaderLock, MT_INFINITE);

    if (WaitResult != WAIT_OBJECT_0) {
        MTSTATUS Status = GetLastStatus();

        // An abandoned mutex is acquired by the waiter.
        if (WaitResult == WAIT_ABANDONED_0) {
            ReleaseMutex(Peb->LoaderData.LoaderLock);
        }

        return Status;
    }

    // Initialize the slot pointer with its count as the size.
    uint64_t SlotCount = Peb->NextTlsIndex;
    void** Slots = NULL;
    MTSTATUS Status = MT_SUCCESS;

    // Make sure we wont overflow the allocation size below
    if (SlotCount > SIZE_MAX / sizeof(*Slots)) {
        Status = MT_NO_RESOURCES;
        goto Cleanup;
    }

    if (SlotCount) {
        Slots = (void**)HeapAlloc(GetProcessHeap(), HEAP_ALLOCATE_ZERO_MEMORY, (size_t)SlotCount * sizeof(void*));

        if (!Slots) {
            Status = GetLastStatus();
            goto Cleanup;
        }
    }

    // Walk the loaded module list and create each module's TLS block.
    PLDR_DATA_TABLE_ENTRY ExecutableModule = NULL;
    PDOUBLY_LINKED_LIST Head = &Peb->LoaderData.LoadedModuleList;
    PDOUBLY_LINKED_LIST Current = Head->Flink;

    void* ExecutableRawAllocation = NULL;
    void* ExecutableThreadPointer = NULL;

    while (Head != Current) {
        PLDR_DATA_TABLE_ENTRY Entry = CONTAINING_RECORD(Current, LDR_DATA_TABLE_ENTRY, LoadedModuleList);

        // This module has no TLS storage
        if (Entry->TlsIndex == MT_INVALID_TLS_INDEX) {
            Current = Current->Flink;
            continue;
        }

        // Make sure the registered index is in the slot snapshot
        if (Entry->TlsIndex >= SlotCount) {
            Status = MT_INVALID_STATE;
            goto Cleanup;
        }

        // Double slot ownership is disallowed
        if (Slots[Entry->TlsIndex] != NULL) {
            Status = MT_INVALID_STATE;
            goto Cleanup;
        }

        bool IsExecutable = Entry->Base == Peb->ImageBase;

        if (IsExecutable) {
            // Only one primary executable
            if (ExecutableModule != NULL) {
                Status = MT_INVALID_STATE;
                goto Cleanup;
            }

            ExecutableModule = Entry;
        }

        // Allocate the module TLS block now
        void* Block = NULL;
        void* RawAllocation = NULL;
        void* ThreadPointer = NULL;

        Status = LdrpAllocateModuleTlsBlock(
            Entry,
            IsExecutable,
            &Block,
            &RawAllocation,
            &ThreadPointer
        );

        if (MT_FAILURE(Status)) {
            goto Cleanup;
        }

        Slots[Entry->TlsIndex] = Block;

        if (IsExecutable) {
            ExecutableRawAllocation = RawAllocation;
            ExecutableThreadPointer = ThreadPointer;
        }

        Current = Current->Flink;
    }
    

    Teb->TlsSlots = Slots;
    Teb->TlsSlotCount = SlotCount;
    Teb->StaticTlsAllocation = ExecutableRawAllocation;
    Teb->ThreadPointer = ExecutableThreadPointer;

    Slots = NULL;
    Status = MT_SUCCESS;

Cleanup:

    // Cleanup any partially created blocks
    if (Slots) {
        for (uint64_t Index = 0; Index < SlotCount; Index++) {
            if (Slots[Index]) {
                void* Allocation = NULL;
                memcpy(
                    &Allocation,
                    (uint8_t*)Slots[Index] - sizeof(Allocation),
                    sizeof(Allocation)
                );

                HeapFree(
                    GetProcessHeap(),
                    HEAP_FREE_NO_OPTIONS,
                    Allocation
                );
            }
        }

        HeapFree(
            GetProcessHeap(),
            HEAP_FREE_NO_OPTIONS,
            Slots
        );
    }

    ReleaseMutex(Peb->LoaderData.LoaderLock);
    return Status;
}

MTSTATUS
LdrpDestroyThreadTls(
    IN OUT PTEB Teb
)

/*++

    Routine description:

        Releases every TLS block owned by the current thread and clears its
        TLS slot array and executable thread pointer.

    Arguments:

        [IN OUT] Teb - Current thread environment block whose TLS state is
        destroyed.

    Return Values:

        MT_SUCCESS when all TLS allocations are released, MT_INVALID_PARAM for
        a foreign TEB, MT_INVALID_STATE for inconsistent TLS state, or the
        current heap failure status.

    Notes:

        This routine is used by the normal MTDLL thread-return path. Forced
        termination does not execute user-mode teardown in the victim thread.

--*/

{
    if (!Teb || Teb != MtCurrentTeb()) return MT_INVALID_PARAM;

    if ((Teb->TlsSlotCount != 0 && !Teb->TlsSlots) ||
        (Teb->StaticTlsAllocation && !Teb->ThreadPointer)) {
        return MT_INVALID_STATE;
    }

    // Stop using the executable TLS allocation before releasing it.
    __asm__ volatile (
        "wrfsbase %0"
        :
        : "r"((void*)NULL)
        : "memory"
    );

    for (uint64_t Index = 0; Index < Teb->TlsSlotCount; Index++) {
        if (!Teb->TlsSlots[Index]) continue;

        MTSTATUS Status = LdrpFreeModuleTlsBlock(Teb->TlsSlots[Index]);
        if (MT_FAILURE(Status)) return Status;

        Teb->TlsSlots[Index] = NULL;
    }

    if (Teb->TlsSlots &&
        !HeapFree(
            GetProcessHeap(),
            HEAP_FREE_NO_OPTIONS,
            Teb->TlsSlots
        )) {
        return GetLastStatus();
    }

    Teb->TlsSlots = NULL;
    Teb->TlsSlotCount = 0;
    Teb->StaticTlsAllocation = NULL;
    Teb->ThreadPointer = NULL;
    return MT_SUCCESS;
}

void
LdrpReleaseCurrentThreadModuleTlsLocked(
    IN PLDR_DATA_TABLE_ENTRY Module
)

/*++

    Routine description:

        Releases the calling thread's materialized TLS block for a module that
        is being rolled back or finally unloaded.

    Arguments:

        [IN] Module - Loader entry whose current-thread TLS block is released.

    Return Values:

        None.

    Notes:

        The caller must hold the loader lock. Other threads retain their blocks
        until normal thread teardown because the first TLS ABI has no TEB
        registry for safe cross-thread reclamation.

--*/

{
    if (!Module || Module->TlsIndex == MT_INVALID_TLS_INDEX) return;

    PTEB Teb = MtCurrentTeb();
    if (!Teb || !Teb->TlsSlots || Module->TlsIndex >= Teb->TlsSlotCount) {
        return;
    }

    void* Block = Teb->TlsSlots[Module->TlsIndex];
    if (!Block) return;

    if (MT_SUCCEEDED(LdrpFreeModuleTlsBlock(Block))) {
        Teb->TlsSlots[Module->TlsIndex] = NULL;
    }
}

static
MTSTATUS
LdrpExpandCurrentThreadTls(
    IN PTEB Teb,
    IN uint64_t ModuleIndex
)

{
    if (!Teb || ModuleIndex == MT_INVALID_TLS_INDEX) {
        return MT_INVALID_PARAM;
    }

    PPEB Peb = Teb->ProcessEnvironmentBlock;
    if (!Peb) {
        return MT_INVALID_STATE;
    }

    uint32_t WaitResult = WaitForSingleObject(
        Peb->LoaderData.LoaderLock,
        MT_INFINITE
    );

    if (WaitResult != WAIT_OBJECT_0) {
        MTSTATUS Status = GetLastStatus();

        // An abandoned mutex is acquired by the waiter.
        if (WaitResult == WAIT_ABANDONED_0) {
            ReleaseMutex(Peb->LoaderData.LoaderLock);
        }

        return Status;
    }

    // Re-check whether the new slot exists after locking
    MTSTATUS Status = MT_SUCCESS;
    if (ModuleIndex < Teb->TlsSlotCount &&
        Teb->TlsSlots &&
        Teb->TlsSlots[ModuleIndex]) {
        goto Cleanup;
    }

    if (Teb->TlsSlotCount != 0 && !Teb->TlsSlots) {
        Status = MT_INVALID_STATE;
        goto Cleanup;
    }

    // Find the module recipe for this process-local TLS index.
    PLDR_DATA_TABLE_ENTRY Module = NULL;
    PDOUBLY_LINKED_LIST Head = &Peb->LoaderData.LoadedModuleList;
    PDOUBLY_LINKED_LIST Current = Head->Flink;

    while (Current != Head) {
        PLDR_DATA_TABLE_ENTRY Entry = CONTAINING_RECORD(
            Current,
            LDR_DATA_TABLE_ENTRY,
            LoadedModuleList
        );

        if (Entry->TlsIndex == ModuleIndex &&
            Entry->State != LdrModuleUnloading) {
            Module = Entry;
            break;
        }

        Current = Current->Flink;
    }

    if (!Module || ModuleIndex >= Peb->NextTlsIndex) {
        Status = MT_NOT_FOUND;
        goto Cleanup;
    }

    uint64_t NewSlotCount = Peb->NextTlsIndex;
    if (NewSlotCount > SIZE_MAX / sizeof(void*)) {
        Status = MT_NO_RESOURCES;
        goto Cleanup;
    }

    void** NewSlots = NULL;
    if (NewSlotCount > Teb->TlsSlotCount || !Teb->TlsSlots) {
        NewSlots = (void**)HeapAlloc(
            GetProcessHeap(),
            HEAP_ALLOCATE_ZERO_MEMORY,
            (size_t)NewSlotCount * sizeof(*NewSlots)
        );

        if (!NewSlots) {
            Status = GetLastStatus();
            goto Cleanup;
        }

        if (Teb->TlsSlots) {
            memcpy(
                NewSlots,
                Teb->TlsSlots,
                (size_t)Teb->TlsSlotCount * sizeof(*NewSlots)
            );
        }
    }

    void* Block = NULL;
    void* RawAllocation = NULL;
    void* ThreadPointer = NULL;
    Status = LdrpAllocateModuleTlsBlock(
        Module,
        false,
        &Block,
        &RawAllocation,
        &ThreadPointer
    );

    if (MT_FAILURE(Status)) {
        if (NewSlots) {
            HeapFree(
                GetProcessHeap(),
                HEAP_FREE_NO_OPTIONS,
                NewSlots
            );
        }
        goto Cleanup;
    }

    if (NewSlots) {
        void** OldSlots = Teb->TlsSlots;
        NewSlots[ModuleIndex] = Block;

        // Publish the complete replacement before releasing the old array.
        Teb->TlsSlots = NewSlots;
        Teb->TlsSlotCount = NewSlotCount;

        if (OldSlots) {
            HeapFree(
                GetProcessHeap(),
                HEAP_FREE_NO_OPTIONS,
                OldSlots
            );
        }
    }
    else {
        Teb->TlsSlots[ModuleIndex] = Block;
    }


Cleanup:
    ReleaseMutex(Peb->LoaderData.LoaderLock);
    return Status;
}

// The function Clang calls for DLL TLS
MTDLL_API
void*
__tls_get_addr(
    IN PMT_TLS_INDEX TlsIndex
)

{
    if (!TlsIndex) return NULL;

    PTEB Teb = MtCurrentTeb();

    if (!Teb) {
        return NULL;
    }

    if (!Teb->TlsSlots ||
        TlsIndex->ModuleIndex >= Teb->TlsSlotCount ||
        !Teb->TlsSlots[TlsIndex->ModuleIndex]) {
        MTSTATUS Status = LdrpExpandCurrentThreadTls(
            Teb,
            TlsIndex->ModuleIndex
        );

        if (MT_FAILURE(Status)) {
            return NULL;
        }
    }

    void* TlsBlock = Teb->TlsSlots[TlsIndex->ModuleIndex];

    return (uint8_t*)TlsBlock + TlsIndex->Offset;
}
