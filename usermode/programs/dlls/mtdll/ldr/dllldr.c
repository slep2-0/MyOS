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

static MTSTATUS
LdrpDereferenceModuleLocked(
    IN PLDR_DATA_TABLE_ENTRY Module
);

static MTSTATUS
LdrpReleaseDependenciesLocked(
    IN PLDR_DATA_TABLE_ENTRY Importer
)

/*++

    Routine description:

        Releases every module reference owned through an importer's dependency
        list. A dependency that loses its final reference is unloaded through
        the normal internal dereference path.

    Arguments:

        [IN] Importer - Loaded module whose recorded dependencies are released.

    Return Values:

        MT_SUCCESS when every dependency is released, or an error status from
        the dependency that could not be dereferenced.

    Notes:

        The caller must hold the process loader lock. Dependency records are
        removed as their corresponding references are released.

--*/

{
    // Loop over all of the list, while the list isnt empty.
    while ((Importer->DependencyListHead.Flink != &Importer->DependencyListHead)) {
        PLDR_DEPENDENCY_ENTRY Entry = CONTAINING_RECORD(Importer->DependencyListHead.Flink, LDR_DEPENDENCY_ENTRY, ListEntry);

        PLDR_DATA_TABLE_ENTRY Dependency = Entry->Module;
        RemoveEntryList(&Entry->ListEntry);
        HeapFree(GetProcessHeap(), HEAP_FREE_NO_OPTIONS, Entry);

        MTSTATUS Status = LdrpDereferenceModuleLocked(Dependency);
        if (MT_FAILURE(Status)) {
            return Status;
        }
    }

    return MT_SUCCESS;
}

static MTSTATUS
LdrpDereferenceModuleLocked(
    IN PLDR_DATA_TABLE_ENTRY Module
)

/*++

    Routine description:

        Releases one reference to a loaded module. When an unpinned module
        reaches zero references, the routine notifies it of process detach,
        releases its dependencies, unmaps its image, and removes its loader
        entry. Pinned modules retain their permanent baseline reference.

    Arguments:

        [IN] Module - Loader entry whose reference is released.

    Return Values:

        MT_SUCCESS when the reference is released, or an error status when the
        module state is invalid or final unloading cannot be completed.

    Notes:

        The caller must hold the process loader lock. This internal path may
        decrement pinned modules, unlike the public FreeLibrary path.

--*/

{
    if (!Module || !Module->ReferenceCount) {
        return MT_INVALID_PARAM;
    }

    if (Module->Pinned && Module->ReferenceCount == 1) {
        // Cannot fully dereference a pinned module (those are MTDLL and the main EXE)
        return MT_INVALID_STATE;
    }

    Module->ReferenceCount--;

    if (Module->Pinned) {
        // Return SUCCESS for pinned at any case.
        return MT_SUCCESS;
    }

    if (Module->ReferenceCount != 0) {
        // The module still has alive references, cannot fully dereference.
        return MT_SUCCESS;
    }

    // Unload the module, it has reached its last reference count, AKA, 0.
    Module->State = LdrModuleUnloading;

    // Call the module's DLL main with the unloading state
    if (Module->EntryPoint) {
        PDLL_ENTRY_POINT Entry = (PDLL_ENTRY_POINT)Module->EntryPoint;
        Entry(Module->Base, DLL_PROCESS_DETACH, NULL);
    }

    // Release its dependencies
    MTSTATUS Status = LdrpReleaseDependenciesLocked(Module);
    if (MT_FAILURE(Status)) {
        return Status;
    }

    // Unmap the module from the file.
    Status = MtUnmapViewOfSection(MtCurrentProcess(), Module->Base);

    if (MT_FAILURE(Status)) {
        return Status;
    }

    // Unlink this module now and return success
    RemoveEntryList(&Module->LoadedModuleList);

    // Finally free the ptr.
    bool Success = HeapFree(GetProcessHeap(), HEAP_FREE_NO_OPTIONS, Module);

    // Return.
    return (Success) ? MT_SUCCESS : MT_GENERAL_FAILURE;
}

MTSTATUS
LdrpReferenceDependency(
    IN PLDR_DATA_TABLE_ENTRY Importer,
    IN const char* ModuleName,
    OUT PLDR_DATA_TABLE_ENTRY* Dependency
)

/*++

    Routine description:

        Records and references a dependency between two loaded modules.

    Arguments:

        [IN] Importer - Module whose import created the dependency.
        [IN] ModuleName - Name of the loaded module.
        [IN] Dependency - Module referenced by the importer.

    Return Values:

        MT_SUCCESS on success, or an error status describing the failure.

--*/

{
    if (!Importer || !ModuleName || !Dependency) {
        return MT_INVALID_PARAM;
    }

    *Dependency = NULL;

    // Search in the Importer dependency list head
    // for the dependancy given, if that dependency
    // is already recorded, return it without incrementing the importer reference count
    // otherwise, call LdrLoadDll which will add a reference count internally, and create an LDR_DEPENDANCY_ENTRY
    // for the importer.
    PLDR_DATA_TABLE_ENTRY LoadedModule =
        LdrFindEntryForModule(ModuleName, MtCurrentPeb(), false);

    // Do not allow circular imports.
    if (LoadedModule &&
        LoadedModule->State != LdrModuleLoaded) {
        return MT_INVALID_STATE;
    }

    if (LoadedModule) {
        PDOUBLY_LINKED_LIST Head = &Importer->DependencyListHead;
        PDOUBLY_LINKED_LIST Current = Head->Flink;

        while (Head != Current) {

            PLDR_DEPENDENCY_ENTRY Entry = CONTAINING_RECORD(Current, LDR_DEPENDENCY_ENTRY, ListEntry);

            if (Entry->Module == LoadedModule) {
                *Dependency = LoadedModule;
                return MT_SUCCESS;
            }

            Current = Current->Flink;
        }
    }

    // Module isnt loaded, or is not present in the importer dependancy chain
    // call LdrLoadDll to either load the module in, or, just increment count for the ref and return it to us in both cases.
    MTSTATUS Status = LdrLoadDll(ModuleName, &LoadedModule);

    if (MT_FAILURE(Status)) {
        return Status;
    }

    // Allocate a dependancy record for the importer and insert it in the list.
    PLDR_DEPENDENCY_ENTRY Entry = (PLDR_DEPENDENCY_ENTRY)HeapAlloc(GetProcessHeap(), HEAP_ALLOCATE_ZERO_MEMORY, sizeof(LDR_DEPENDENCY_ENTRY));
    if (!Entry) {
        LdrpDereferenceModuleLocked(LoadedModule);
        return MT_NO_MEMORY;
    }

    Entry->Module = LoadedModule;
    InsertTailList(&Importer->DependencyListHead, &Entry->ListEntry);
    *Dependency = LoadedModule;
    return MT_SUCCESS;
}

MTSTATUS
LdrLoadDll(
    IN const char* DllPath,
    OUT PLDR_DATA_TABLE_ENTRY* DllEntry
)

/*++

    Routine description:

        Loads a DLL and returns its loader data-table entry.

    Arguments:

        [IN] DllPath - Path of the library to load.
        [IN] DllEntry - Loader entry describing the library.

    Return Values:

        MT_SUCCESS on success, or an error status describing the failure.

--*/

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
    NewEntry->Pinned = false;
    strncpy(NewEntry->FullName, DllPath, sizeof(NewEntry->FullName));
    InitializeListHead(&NewEntry->LoadedModuleList);
    InitializeListHead(&NewEntry->DependencyListHead);

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

    if (MT_FAILURE(Status) && NewEntry) {
        LdrpReleaseDependenciesLocked(NewEntry);
    }

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

MTSTATUS
LdrUnloadDll(
    IN HMODULE Module
)

/*++

    Routine description:

        Releases a DLL reference and unloads the image when its count reaches zero.

    Arguments:

        [IN] Module - Loaded-module entry affected by the operation.

    Return Values:

        MT_SUCCESS on success, or an error status describing the failure.

--*/

{
    if (!Module) return MT_INVALID_PARAM;

    // Acquire the loader lock first.
    uint32_t WaitStatus = WaitForSingleObject(MtCurrentPeb()->LoaderData.LoaderLock, MT_INFINITE);

    if (WaitStatus == WAIT_ABANDONED) {
        // Mutex abandonment, do not iterate over the list.
        MTSTATUS AbandonStatus = GetLastStatus();
        ReleaseMutex(MtCurrentPeb()->LoaderData.LoaderLock);
        return AbandonStatus;
    }

    if (WaitStatus != WAIT_OBJECT_0) {
        // Mutex could not be held.
        return GetLastStatus();
    }

    PDOUBLY_LINKED_LIST Head = &MtCurrentPeb()->LoaderData.LoadedModuleList;
    PDOUBLY_LINKED_LIST Current = Head->Flink;
    MTSTATUS Status = MT_NOT_FOUND;

    while (Head != Current) {

        PLDR_DATA_TABLE_ENTRY Entry = CONTAINING_RECORD(Current, LDR_DATA_TABLE_ENTRY, LoadedModuleList);

        if (Entry->Base == Module) {
            // Found the DLL to unload,
            // First the DLL must not be in a Loading state
            if (Entry->State != LdrModuleLoaded) {
                Status =  MT_INVALID_STATE;
                goto Cleanup;
            }

            // A pinned module cannot be unloaded
            if (Entry->Pinned) {
                Status = MT_ACCESS_DENIED;
                goto Cleanup;
            }

            // The module can maybe be unloaded
            // we will let the internal helper do this for us here
            // it will decrement reference count, and if it reaches zero
            // will recursively dereference all dependencies that this DLL has held
            Status = LdrpDereferenceModuleLocked(Entry);
            goto Cleanup;
        }

        Current = Current->Flink;
    }

Cleanup:
    ReleaseMutex(MtCurrentPeb()->LoaderData.LoaderLock);
    return Status;
}
