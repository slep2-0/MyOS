/*++

Module Name:

    dllldr.c

Purpose:

    This translation unit contains the implementation of loading DLLs into user space.

Author:

    slep (Matanel) 2026.

Revision History:

--*/

#include "../includes/mtdll.h"
#include "../includes/errorhandlingapi.h"
#include "../includes/ioapi.h"
#include "mte.h"

MTSTATUS
LdrLoadDll(
    IN const char* DllPath,
    OUT PLDR_DATA_TABLE_ENTRY* DllEntry
)

{
    if (!DllPath || !DllEntry) return MT_INVALID_PARAM;

    // DllEntry starts as NULL initially
    *DllEntry = NULL;
    PLDR_DATA_TABLE_ENTRY NewEntry = NULL;
    bool EntryLinked = false;

    // Acquire load lock mutex
    uint32_t WaitResult = WaitForSingleObject(
        MtCurrentPeb()->LoaderData.LoaderLock,
        MT_INFINITE
    );

    if (WaitResult == WAIT_FAILED) {
        return GetLastStatus();
    }

    if (WaitResult == WAIT_ABANDONED_0) {
        MTSTATUS AbandonedStatus = GetLastStatus();
        ReleaseMutex(MtCurrentPeb()->LoaderData.LoaderLock);
        return AbandonedStatus;
    }

    bool LoaderLockHeld = true;
    bool MappedView = false;
    void* BaseAddress = NULL;
    MTSTATUS Status = MT_SUCCESS;

    // First, check in the PEB loaded module list for the DllPath, so we dont double load a DLL.
    // this can simply be if the ptr is valid.
    PLDR_DATA_TABLE_ENTRY Entry = LdrFindEntryForModule(DllPath, MtCurrentPeb(), true);

    if (Entry) {

        if (Entry->State == LdrModuleLoaded) {
            Entry->ReferenceCount++;
            *DllEntry = Entry;
        }
        else {
            // Circular imports are not yet supported
            Status = MT_INVALID_STATE;
        }

        goto Cleanup;
    }

    // DLL Is not loaded in the process, we must load it ourselves.
    // Open the DLL with read access.
    HANDLE FileHandle = MT_INVALID_HANDLE;
    Status = MtCreateFile(DllPath, MT_FILE_READ_DATA, FILE_OPEN_EXISTING, &FileHandle);

    if (MT_FAILURE(Status)) goto Cleanup;

    // Good, file is opened, create the section for the DLL, and then close the handle for the file
    // the section retains its own file object
    HANDLE SectionHandle = MT_INVALID_HANDLE;
    Status = MtCreateSection(&SectionHandle, MT_SECTION_MAP_READ | MT_SECTION_MAP_WRITE | MT_SECTION_MAP_EXECUTE, FileHandle);
    MtClose(FileHandle);

    if (MT_FAILURE(Status)) {
        goto Cleanup;
    }

    // Map the section into the process now, and then close the section handle since the VAD Keeps the file object alive
    void* EntryPointAddress = NULL;
    size_t ViewSize = 0;
    Status = MtMapViewOfSection(SectionHandle, MtCurrentProcess(), &BaseAddress, &EntryPointAddress, &ViewSize);
    MtClose(SectionHandle);

    if (MT_FAILURE(Status)) {
        goto Cleanup;
    }

    MappedView = true;

    // Validate outputs are correct
    if (BaseAddress == NULL || ViewSize < sizeof(MTE_HEADER)) {
        Status = MT_INVALID_IMAGE_FORMAT;
        goto Cleanup;
    }

    // Validate the MT image, first validate the header.
    uint8_t* Base = (uint8_t*)BaseAddress;
    if (Base[0] != 'M' || Base[1] != 'T' || Base[2] != 'E' || Base[3] != 0) {
        // Invalid MTE header.
        Status = MT_INVALID_IMAGE_FORMAT;
        goto Cleanup;
    }

    PMTE_HEADER Header = (PMTE_HEADER)BaseAddress;

    // If there are relocations to be made, then apply them now.
    if (Header->reloc_size != 0) {

        // The relocation table must contain only complete relocation records.
        if (Header->reloc_size % sizeof(MTE_RELOCATION) != 0) {
            Status = MT_INVALID_IMAGE_FORMAT;
            goto Cleanup;
        }

        // Validate that the complete relocation table lies inside the mapped image.
        if (Header->reloc_rva > ViewSize ||
            Header->reloc_size > ViewSize - Header->reloc_rva) {
            Status = MT_INVALID_IMAGE_FORMAT;
            goto Cleanup;
        }

        size_t RelocationCount = Header->reloc_size / sizeof(MTE_RELOCATION);
        PMTE_RELOCATION RelocationBase = (PMTE_RELOCATION)((uintptr_t)BaseAddress + Header->reloc_rva);

        for (size_t i = 0; i < RelocationCount; i++) {
            PMTE_RELOCATION RelocationEntry = &RelocationBase[i];

            // The relocation info must be the supported relative relocation only.
            if (RelocationEntry->r_info != MTE_RELOCATION_X86_64_RELATIVE) {
                Status = MT_INVALID_IMAGE_FORMAT;
                goto Cleanup;
            }

            // Validate the offset is inside the image and it does not overflow.
            if (RelocationEntry->r_offset > ViewSize ||
                sizeof(uint64_t) > ViewSize - RelocationEntry->r_offset) {
                Status = MT_INVALID_IMAGE_FORMAT;
                goto Cleanup;
            }

            if (RelocationEntry->r_addend < 0 || (uint64_t)RelocationEntry->r_addend >= ViewSize) {
                Status = MT_INVALID_IMAGE_FORMAT;
                goto Cleanup;
            }

            uint64_t* Target = (uint64_t*)((uintptr_t)BaseAddress + RelocationEntry->r_offset);
            *Target = (uint64_t)((uintptr_t)BaseAddress + RelocationEntry->r_addend);
        }
    }

    // Allocate a new loader data table entry for this DLL
    NewEntry = (PLDR_DATA_TABLE_ENTRY)HeapAlloc(GetProcessHeap(), HEAP_ALLOCATE_ZERO_MEMORY, sizeof(LDR_DATA_TABLE_ENTRY));

    if (!NewEntry) {
        Status = GetLastStatus();
        goto Cleanup;
    }

    // Initialize the entry now
    NewEntry->Base = BaseAddress;
    NewEntry->EntryPoint = EntryPointAddress;
    NewEntry->SizeOfImage = ViewSize;
    NewEntry->LoadTime = 0; // no usermode time query api yet..
    NewEntry->ReferenceCount = 1;
    NewEntry->State = LdrModuleLoading;
    strncpy(NewEntry->FullName, DllPath, sizeof(NewEntry->FullName));
    InitializeListHead(&NewEntry->LoadedModuleList);

    // Link to the TEB now.
    InsertTailList(&MtCurrentPeb()->LoaderData.LoadedModuleList, &NewEntry->LoadedModuleList);
    EntryLinked = true;

    // Process this DLL imports now.
    Status = LdrpProcessImports(NewEntry, MtCurrentPeb());
    if (MT_FAILURE(Status)) goto Cleanup;

    // Good, now call the DllMain for this DLL.
    if (NewEntry->EntryPoint) {
        PDLL_ENTRY_POINT EntryPoint = (PDLL_ENTRY_POINT)NewEntry->EntryPoint;

        // If DllMain returned false, do not load this DLL in.
        if (!EntryPoint(NewEntry->Base, DLL_PROCESS_ATTACH, NULL)) {
            Status = MT_DLL_INITIALIZATION_FAILED;
            goto Cleanup;
        }
    }

    NewEntry->State = LdrModuleLoaded;
    *DllEntry = NewEntry;

Cleanup:

    if (MT_FAILURE(Status) && MappedView) {
        MtUnmapViewOfSection(MtCurrentProcess(), BaseAddress);
    }

    if (MT_FAILURE(Status) && NewEntry) {
        if (EntryLinked) {
            RemoveEntryList(&NewEntry->LoadedModuleList);
        }

        HeapFree(GetProcessHeap(), HEAP_FREE_NO_OPTIONS, NewEntry);
    }

    // Mutex release must be last so another thread wont observe corrupted data
    if (LoaderLockHeld) {
        ReleaseMutex(MtCurrentPeb()->LoaderData.LoaderLock);
    }

    return Status;
}
