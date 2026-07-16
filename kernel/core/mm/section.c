/*++

Module Name:

    section.c

Purpose:

    This translation unit contains the implementation of file sections (process sections).

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "../../includes/mm.h"
#include "../../includes/ob.h"
#include "../../includes/mg.h"
#include "../../includes/fs.h"
#include "../../includes/ps.h"

static bool
MmpIsFileRangeValid(
    IN uint64_t Offset,
    IN uint64_t Size,
    IN uint64_t FileSize
)
{
    if (Size == 0) return true;
    return Offset < FileSize && Size <= FileSize - Offset;
}

MTSTATUS
MmCreateSection(
    OUT void** SectionObject,
    IN struct _FILE_OBJECT* FileObject
)
{
    if (!SectionObject || !FileObject) return MT_INVALID_PARAM;

    MTE_HEADER Header;
    MTSTATUS Status;
    size_t BytesRead = 0;
    // Assume failure.
    *SectionObject = NULL;

    // Read the header from the file.
    Status = FsReadFile(
        FileObject,
        0,
        &Header,
        sizeof(MTE_HEADER),
        &BytesRead
    );
    if (MT_FAILURE(Status)) return Status;
    if (BytesRead != sizeof(MTE_HEADER)) return MT_INVALID_IMAGE_FORMAT;

    // Validate header magic.
    if (kmemcmp(Header.Magic, "MTE\0", 4) != 0) {
        // Invalid header.
#ifdef DEBUG
        gop_printf(COLOR_RED, "Invalid executable given, magic is not MTE.\n");
#endif
        return MT_INVALID_IMAGE_FORMAT;
    }

    uint64_t FileEndRVA = FileObject->FileSize;
    if (FileEndRVA < sizeof(MTE_HEADER) ||
        Header.PreferredImageBase < USER_VA_START ||
        Header.PreferredImageBase > MmHighestUserAddress ||
        (Header.PreferredImageBase & (VirtualPageSize - 1)) != 0 ||
        Header.EntryRVA >= FileEndRVA ||
        !MmpIsFileRangeValid(Header.TextRVA, Header.TextSize, FileEndRVA) ||
        !MmpIsFileRangeValid(Header.DataRVA, Header.DataSize, FileEndRVA) ||
        !MmpIsFileRangeValid(Header.exports_rva, Header.exports_size, FileEndRVA) ||
        !MmpIsFileRangeValid(Header.reloc_rva, Header.reloc_size, FileEndRVA) ||
        !MmpIsFileRangeValid(Header.imports_rva, Header.imports_size, FileEndRVA) ||
        Header.exports_size % sizeof(MT_EXPORT_ENTRY) != 0 ||
        Header.reloc_size % sizeof(Rela) != 0 ||
        Header.imports_size % sizeof(MT_IMPORT_ENTRY) != 0 ||
        WILL_ADD_OVERFLOW(FileEndRVA, Header.BssSize) ||
        FileEndRVA + Header.BssSize > UINTPTR_MAX - (VirtualPageSize - 1)) {
        return MT_INVALID_IMAGE_FORMAT;
    }

    uint64_t ImageSize = ALIGN_UP(
        FileEndRVA + Header.BssSize,
        VirtualPageSize
    );
    if (ImageSize == 0 ||
        ImageSize - 1 > MmHighestUserAddress - Header.PreferredImageBase) {
        return MT_INVALID_IMAGE_FORMAT;
    }

    if (Header.EntryRVA != 0 && Header.TextSize != 0 &&
        (Header.EntryRVA < Header.TextRVA ||
         Header.EntryRVA - Header.TextRVA >= Header.TextSize)) {
        return MT_INVALID_IMAGE_FORMAT;
    }

    // Allocate the actual section object (pool)
    PMM_SECTION NewSection = NULL;
    Status = ObCreateObject(MmSectionType, sizeof(MM_SECTION), (void**)&NewSection);
    if (MT_FAILURE(Status)) return Status;

    if (!ObReferenceObject(FileObject)) {
        ObDereferenceObject(NewSection);
        return MT_OBJECT_DELETED;
    }

    // Set fields
    NewSection->FileObject = FileObject;
    NewSection->EntryPointOffset = Header.EntryRVA;
    NewSection->PreferredBase = Header.PreferredImageBase;

    // Create subsections

    // Setup .text - Read | Execute
    NewSection->Text.FileOffset = Header.TextRVA;
    NewSection->Text.VirtualSize = Header.TextSize;
    NewSection->Text.Protection = VAD_FLAG_READ | VAD_FLAG_EXECUTE | VAD_FLAG_MAPPED_FILE;
    NewSection->Text.IsDemandZero = 0;

    // Setup .data - Read | Write | CopyOnWrite
    NewSection->Data.FileOffset = Header.DataRVA;
    NewSection->Data.VirtualSize = Header.DataSize;
    NewSection->Data.Protection = VAD_FLAG_READ | VAD_FLAG_WRITE | VAD_FLAG_MAPPED_FILE | VAD_FLAG_COPY_ON_WRITE;
    NewSection->Data.IsDemandZero = 0;

    // Setup .bss (uninit) - Read | Write | DemandZero
    NewSection->Bss.FileOffset = 0; // Irrelevant for BSS
    NewSection->Bss.VirtualSize = Header.BssSize;
    NewSection->Bss.Protection = VAD_FLAG_READ | VAD_FLAG_WRITE;
    NewSection->Bss.IsDemandZero = 1;

    // Configure the WholeFileSection.
    // This represents the chunk of virtual memory that maps directly to the file.
    // It starts at FileOffset 0 (so we can see the Header) and goes up to the end of Data.
    NewSection->WholeFileSection.FileOffset = 0;
    NewSection->WholeFileSection.VirtualSize = FileEndRVA;

    // We default to RWX here to simplify loading; permissions should be refined later via VirtualProtect.
    NewSection->WholeFileSection.Protection = VAD_FLAG_READ | VAD_FLAG_WRITE | VAD_FLAG_EXECUTE | VAD_FLAG_MAPPED_FILE;
    NewSection->WholeFileSection.IsDemandZero = 0;

    // Calculate total size of the image in memory.
    // This includes the file part + the BSS part.
    NewSection->ImageSize = ImageSize;

    // Set the section object as the new section.
    *SectionObject = NewSection;

    // Successful!
    return MT_SUCCESS;
}

MTSTATUS
MmMapViewOfSection(
    IN void* SectionObject,
    IN PEPROCESS Process,
    OUT void** EntryPointAddress,
    OUT void** BaseAddress
)
{
    PMM_SECTION Section = (PMM_SECTION)SectionObject;

    if (!SectionObject || !Process || !EntryPointAddress || !BaseAddress) {
        return MT_INVALID_PARAM;
    }

    *EntryPointAddress = NULL;
    *BaseAddress = NULL;

    uintptr_t load_base = Section->PreferredBase;

    // Map the whole file, header + text + data.
    // We attempt to map the file content at the preferred base.
    MTSTATUS Status = MmAllocateVirtualMemory(
        Process,
        (void**)&load_base,
        Section->WholeFileSection.VirtualSize,
        Section->WholeFileSection.Protection
    );

    if (MT_FAILURE(Status)) {
        // Preferred image base is taken. Let the VAD allocator pick an address.
        // Relocation tables (mapped inside this chunk) will be needed to fix addresses.
        load_base = 0;
        Status = MmAllocateVirtualMemory(
            Process,
            (void**)&load_base,
            Section->WholeFileSection.VirtualSize,
            Section->WholeFileSection.Protection
        );
    }

    if (MT_FAILURE(Status)) goto Cleanup;

    if (load_base > MmHighestUserAddress || Section->ImageSize == 0 ||
        Section->ImageSize - 1 > MmHighestUserAddress - load_base) {
        void* AllocationBase = (void*)load_base;
        size_t AllocationSize = Section->WholeFileSection.VirtualSize;
        MmFreeVirtualMemory(
            Process,
            &AllocationBase,
            &AllocationSize,
            MEM_RELEASE
        );
        Status = MT_INVALID_ADDRESS;
        goto Cleanup;
    }

    // Store the file and fileoffset into the vad we just got.
    // IMPORTANT: We map from FileOffset 0. This exposes the MTE Header in memory.
    MsAcquirePushLockExclusive(&Process->VadLock);
    PMMVAD Vad = MiFindVadInternal(Process, load_base, false);
    if (!Vad || (Section->FileObject && !ObReferenceObject(Section->FileObject))) {
        MsReleasePushLockExclusive(&Process->VadLock);
        void* AllocationBase = (void*)load_base;
        size_t AllocationSize = Section->WholeFileSection.VirtualSize;
        MmFreeVirtualMemory(
            Process,
            &AllocationBase,
            &AllocationSize,
            MEM_RELEASE
        );
        Status = MT_NOT_FOUND;
        goto Cleanup;
    }

    Vad->File = Section->FileObject;
    Vad->FileOffset = Section->WholeFileSection.FileOffset; // 0
    Vad->Flags |= VAD_FLAG_MAPPED_FILE;
    MsReleasePushLockExclusive(&Process->VadLock);

    // .bss lives immediately after the file data in Virtual Memory.
    if (Section->Bss.VirtualSize > 0) {
        // The RVA where BSS logically starts
        uintptr_t BssStartVa = load_base + Section->WholeFileSection.VirtualSize;
        // The RVA where BSS ends
        uintptr_t BssEndVa = BssStartVa + Section->Bss.VirtualSize;

        // The start of the NEXT page after the file data
        uintptr_t NextPageVa = ALIGN_UP(BssStartVa, VirtualPageSize);

        // 2. Allocate the overflow
        // Only if BSS is large enough to cross into the next page
        if (BssEndVa > NextPageVa) {
            uintptr_t OverflowSize = BssEndVa - NextPageVa;
            uintptr_t AllocBase = NextPageVa;

            Status = MmAllocateVirtualMemory(
                Process,
                (void**)&AllocBase, // Must be page aligned
                OverflowSize,
                Section->Bss.Protection
            );

            if (MT_FAILURE(Status)) {
                void* load_Base_temp = (void*)load_base;
                size_t AllocationSize = Section->WholeFileSection.VirtualSize;
                MmFreeVirtualMemory(Process, &load_Base_temp, &AllocationSize, MEM_RELEASE);
                goto Cleanup;
            }
        }
    }

    // The true base address is at load_base
    *BaseAddress = (void*)load_base;

    // Compute RIP based on where we actually loaded
    uintptr_t RipAddress = load_base + Section->EntryPointOffset;
    *EntryPointAddress = (void*)RipAddress;

Cleanup:
    return Status;
}

void
MmpDeleteSection(
    void* Object
)
{
    PMM_SECTION Section = (PMM_SECTION)Object;

    // Deref the file object if it exists.
    if (Section->FileObject) {
        ObDereferenceObject(Section->FileObject);
    }
}
