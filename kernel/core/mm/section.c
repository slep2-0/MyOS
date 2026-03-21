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

MTSTATUS
MmCreateSection(
    OUT void** SectionObject,
    IN struct _FILE_OBJECT* FileObject
)
{
    MTE_HEADER Header;
    MTSTATUS Status;
    // Assume failure.
    *SectionObject = NULL;

    // Read the header from the file.
    Status = FsReadFile(FileObject, 0, &Header, sizeof(MTE_HEADER), NULL);
    if (MT_FAILURE(Status)) {
        return Status;
    }

    // Validate header magic.
    if (kmemcmp(Header.Magic, "MTE\0", 4) != 0) {
        // Invalid header.
#ifdef DEBUG
        gop_printf(COLOR_RED, "Invalid executable given, magic is not MTE.\n");
#endif
        return MT_INVALID_IMAGE_FORMAT;
    }

    // Allocate the actual section object (pool)
    PMM_SECTION NewSection = NULL;
    Status = ObCreateObject(MmSectionType, sizeof(MM_SECTION), (void**)&NewSection);
    if (MT_FAILURE(Status)) return Status;

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

    // The file end RVA is just the file size.
    uintptr_t FileEndRVA = FileObject->FileSize;

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
    NewSection->ImageSize = ALIGN_UP(FileEndRVA + Header.BssSize, VirtualPageSize);

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

    // Store the file and fileoffset into the vad we just got.
    // IMPORTANT: We map from FileOffset 0. This exposes the MTE Header in memory.
    PMMVAD Vad = MiFindVad(Process, load_base);
    if (Vad) {
        Vad->File = Section->FileObject;
        Vad->FileOffset = Section->WholeFileSection.FileOffset; // 0
    }

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
                MmFreeVirtualMemory(Process, &load_Base_temp, &OverflowSize, MEM_RELEASE);
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