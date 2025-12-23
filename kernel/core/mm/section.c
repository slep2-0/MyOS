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
    OUT PHANDLE SectionHandle,
    IN struct _FILE_OBJECT* FileObject
)

{
    MTE_HEADER Header;
    MTSTATUS Status;
    // Assume failure.
    *SectionHandle = 0;

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
    NewSection->Bss.FileOffset = 0; // Irrelevant
    NewSection->Bss.VirtualSize = Header.BssSize;
    NewSection->Bss.Protection = VAD_FLAG_READ | VAD_FLAG_WRITE;
    NewSection->Bss.IsDemandZero = 1;

    // Calculate total size of sections.
    // This is a rough estimate (aligns up)
    // We could also not align it up, and the receiver does, but i dont trust myself.
    NewSection->ImageSize = ALIGN_UP(Header.TextSize + Header.DataSize + Header.BssSize, VirtualPageSize);

    // Create a handle for the section.
    Status = ObCreateHandleForObject(NewSection, MT_SECTION_ALL_ACCESS, SectionHandle);

    // Successful!
    // If success on ObCreateHandleForObject it would dereference the pointer count created by ObCreateObject (cancel out the reference made by ObCreateHandleForObject)
    // And so HandleCount == PointerCount
    // Else, it would destroy the section (along with the file handle)
    ObDereferenceObject(NewSection);
    return MT_SUCCESS;
}

MTSTATUS 
MmMapViewOfSection(
    IN HANDLE SectionHandle,
    IN PEPROCESS Process,
    OUT void** BaseAddress
)

{
    MTSTATUS Status;
    uintptr_t load_base = 0;
    uintptr_t mapped_text_va = 0;
    uintptr_t DataVa = 0;

    PMM_SECTION Section; 
    Status = ObReferenceObjectByHandle(SectionHandle, MT_SECTION_ALL_ACCESS, MmSectionType, (void**)&Section, NULL);
    if (MT_FAILURE(Status)) return Status;

    // Map .text
    if (Section->Text.VirtualSize > 0) {
        // Request a VA from the gap, if this is in the initial process executable, it should be at 0x10000
        // If this is a DLL, it would return a VA for us, anywhere.
        Status = MmAllocateVirtualMemory(
            Process,
            (void**)&mapped_text_va,
            Section->Text.VirtualSize,
            Section->Text.Protection
        );
        if (MT_FAILURE(Status)) return Status;
        // Store the file and fileoffset into the vad we just got.
        PMMVAD Vad = MiFindVad(Process, mapped_text_va);
        if (Vad) {
            Vad->File = Section->FileObject;
            Vad->FileOffset = Section->Text.FileOffset;
        }
    }

    load_base = mapped_text_va - Section->Text.FileOffset;

    // Map .data
    if (Section->Data.VirtualSize > 0) {
        DataVa = load_base + Section->Data.FileOffset;
        Status = MmAllocateVirtualMemory(
            Process,
            (void**)&DataVa,
            Section->Data.VirtualSize,
            Section->Data.Protection
        );
        if (MT_FAILURE(Status)) {
            MmFreeVirtualMemory(Process, (void*)mapped_text_va);
            return Status;
        }
        // Store the file and fileoffset into the vad we just got.
        PMMVAD Vad = MiFindVad(Process, DataVa);
        if (Vad) {
            Vad->File = Section->FileObject;
            Vad->FileOffset = Section->Data.FileOffset;
        }
    }

    // Map .bss
    // We do not have a file for .bss since its demand zero.
    if (Section->Bss.VirtualSize > 0) {
        uintptr_t BssVa = DataVa + Section->Data.VirtualSize;
        Status = MmAllocateVirtualMemory(
            Process,
            (void**)&BssVa,
            Section->Bss.VirtualSize,
            Section->Bss.Protection
        );
        if (MT_FAILURE(Status)) {
            MmFreeVirtualMemory(Process, (void*)mapped_text_va);
            MmFreeVirtualMemory(Process, (void*)DataVa);
            return Status;
        }
    }

    // Compute RIP
    uintptr_t RipAddress = load_base + Section->EntryPointOffset;
    *BaseAddress = (void*)RipAddress;

    return MT_SUCCESS;
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