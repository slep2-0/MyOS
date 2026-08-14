/*++

Module Name:

    procldr.c

Purpose:

    This translation unit contains the implementation of loading processes into the user space correctly (setting up IAT, etc)

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "../includes/mtdll.h"
#include "../includes/exports.h"
#include "../includes/errorhandlingapi.h"
#include "mte.h"
#include "../includes/ioapi.h"

// 1. LDR_DATA_TABLE_ENTRY of Dll.
// 2. "WriteFile"
// 3. The address of the replaceable pointer in .data

static
bool
LdrpImageRangeValid(
    IN uint64_t Rva,
    IN uint64_t Size,
    IN uint64_t ImageSize
)

/*++

    Routine description:

        Verifies that an image-relative range fits within an image.

    Arguments:

        [IN] Rva - The image-relative start offset.
        [IN] Size - The range size in bytes.
        [IN] ImageSize - The total image size in bytes.

    Return Values:

        true when the range is valid, or false when it overflows or exceeds
        the image.

--*/
{
    if (Size == 0) return Rva <= ImageSize;
    return Rva < ImageSize && Size <= ImageSize - Rva;
}

static
const char*
LdrpImageString(
    IN uint8_t* ImageBase,
    IN uint64_t ImageSize,
    IN uint64_t StringRva
)

/*++

    Routine description:

        Resolves an image-relative string after validating that it is bounded
        and null-terminated inside the image.

    Arguments:

        [IN] ImageBase - The loaded image base.
        [IN] ImageSize - The total image size in bytes.
        [IN] StringRva - The image-relative string offset.

    Return Values:

        A pointer to the string on success, or NULL for an invalid or
        unterminated string.

--*/
{
    if (!LdrpImageRangeValid(StringRva, 1, ImageSize)) return NULL;

    const char* String = (const char*)(ImageBase + StringRva);
    for (uint64_t Index = StringRva; Index < ImageSize; Index++) {
        if (ImageBase[Index] == '\0') return String;
    }
    return NULL;
}

MTSTATUS
LdrpGetProcedureAddress(
    IN PLDR_DATA_TABLE_ENTRY DllEntry,
    IN const char* FunctionName,
    OUT void** ProcdureAddress
)

/*++

    Routine description:

        Resolves one imported function against a loaded module's export table.

    Arguments:

        [IN] DllEntry - The loaded module whose exports are searched.
        [IN] FunctionName - The imported function name.
        [OUT] ProcdureAddress - Receives the resolved function address.

    Return Values:

        MT_SUCCESS when the export is resolved, MT_NOT_FOUND when it is not
        present, or an image-format status when the module metadata is invalid.

--*/

// Shouldnt we use an array of function names and IatSlots so we can fill them in faster instead of function calling each iteration?

{
    if (!DllEntry || !FunctionName || !ProcdureAddress || !DllEntry->Base ||
        DllEntry->SizeOfImage < sizeof(MTE_HEADER)) {
        return MT_INVALID_IMAGE_FORMAT;
    }

    // Grab image base.
    uint8_t* ImageBase = (uint8_t*)DllEntry->Base;
    uint64_t ImageSize = DllEntry->SizeOfImage;

    // Use the export table of the DllEntry given to load into the Iat.
    MTE_HEADER* Header = (MTE_HEADER*)ImageBase;

    if (Header->Magic[0] != 'M' || Header->Magic[1] != 'T' ||
        Header->Magic[2] != 'E' || Header->Magic[3] != '\0' ||
        Header->exports_size == 0 ||
        Header->exports_size % sizeof(MT_EXPORT_ENTRY) != 0 ||
        !LdrpImageRangeValid(
            Header->exports_rva,
            Header->exports_size,
            ImageSize
        )) {
        return MT_INVALID_IMAGE_FORMAT;
    }

    // Iterate over the export table to find the required export for the import.
    MT_EXPORT_ENTRY* ExportTable = (MT_EXPORT_ENTRY*)(ImageBase + Header->exports_rva);
    size_t ExportCount = Header->exports_size / sizeof(MT_EXPORT_ENTRY);

    // Iterate over the imports.
    for (size_t i = 0; i < ExportCount; i++) {
        MT_EXPORT_ENTRY* Entry = &ExportTable[i];

        const char* ExportFunctionName = LdrpImageString(
            ImageBase,
            ImageSize,
            Entry->name_rva
        );
        if (!ExportFunctionName ||
            !LdrpImageRangeValid(Entry->func_rva, 1, ImageSize)) {
            return MT_INVALID_IMAGE_FORMAT;
        }
        void* ExportFunctionAddress = (void*)(ImageBase + Entry->func_rva);

        // If this is the function that the import required, we now use it.
        if (strcmp(FunctionName, ExportFunctionName) == 0) {
            // This is it! Replace ptr.
            *ProcdureAddress = ExportFunctionAddress;
            return MT_SUCCESS;
        }
    }

    // Couldn't find the function..
    return MT_NOT_FOUND;
}

static bool GetBaseName(const char* fullpath, char* out, size_t outsz)

/*++

    Routine description:

        Extracts the final component of a module path into a caller buffer.

    Arguments:

        [IN] fullpath - The module path.
        [OUT] out - The destination name buffer.
        [IN] outsz - The destination buffer size in bytes.

    Return Values:

        true when the name fits and is copied, or false for invalid input or
        insufficient output space.

--*/
{
    if (!fullpath || !out || outsz == 0) return false;

    size_t len = strlen(fullpath);
    const char* p = fullpath + len;

    // Find the last slash
    while (p > fullpath && *(p - 1) != '/') --p;

    size_t name_len = strlen(p);
    if (name_len + 1 > outsz) return false;

    // Copy the name this time, dont enforce .mtexe like in process.c
    // The caller should verify.
    strncpy(out, p, name_len + 1);

    return true;
}

PLDR_DATA_TABLE_ENTRY
LdrFindEntryForModule(
    IN const char* ModuleName,
    IN PPEB Peb,
    IN bool UseFullPath
)

/*++

    Routine description:

        Searches the PEB loader list for a module by its base name.

    Arguments:

        [IN] ModuleName - The module name to find.
        [IN] Peb - The PEB whose loader list is searched.
        [IN] UseFullPath - A boolean value indicating whether to compare ModuleName with the DLL
                           full path in the PEB, or just its base name. (C:\Example\test.dll vs test.dll)

    Return Values:

        The matching loader entry, or NULL when no entry matches.

--*/

{
    // We iterate over the PEB and see if we found it.
    // I took the PebPointer as an argument since we cant do Teb->Peb because we might be at a point where we havent setupped the main thread yet.
    PDOUBLY_LINKED_LIST ListHead = &Peb->LoaderData.LoadedModuleList;
    PDOUBLY_LINKED_LIST Curr = ListHead->Flink;

    while (ListHead != Curr) {
        // Get LDR.
        PLDR_DATA_TABLE_ENTRY Entry = CONTAINING_RECORD(Curr, LDR_DATA_TABLE_ENTRY, LoadedModuleList);

        // String compare.
        char ImageName[256];

        if (!UseFullPath) {
            if (!GetBaseName(Entry->FullName, ImageName, sizeof(ImageName))) goto AdvancePtr;
        }
        else {
            strcpy(ImageName, Entry->FullName);
        }

        if (strcmp(ModuleName, ImageName) == 0) {
            // Found it!
            return Entry;
        }

        AdvancePtr:
        // Advance.
        Curr = Curr->Flink;
    }

    return NULL;
}

MTSTATUS 
LdrpProcessImports(
    IN PLDR_DATA_TABLE_ENTRY ExecutableEntry,
    IN PPEB PebPointer
)

/*++

    Routine description:

        Walks an image's import table and patches each validated IAT slot.

    Arguments:

        [IN] ExecutableEntry - The loaded image whose imports are processed.
        [IN] PebPointer - The PEB used to locate already loaded modules.

    Return Values:

        MT_SUCCESS when every import is processed, MT_NOT_FOUND when a module
        or function is absent, MT_NOT_IMPLEMENTED for an unsupported library,
        or an image-format status for malformed metadata.

--*/

{
    // Declaration of status (function scope)
    MTSTATUS Status = MT_SUCCESS;

    if (!ExecutableEntry || !PebPointer || !ExecutableEntry->Base ||
        ExecutableEntry->SizeOfImage < sizeof(MTE_HEADER)) {
        return MT_INVALID_IMAGE_FORMAT;
    }

    // Get the image base of the executable.
    uint8_t* ImageBase = (uint8_t*)ExecutableEntry->Base;
    uint64_t ImageSize = ExecutableEntry->SizeOfImage;

    // 2. Read the MTE Header (Assumes header is at offset 0)
    MTE_HEADER* Header = (MTE_HEADER*)ImageBase;

    if (Header->Magic[0] != 'M' || Header->Magic[1] != 'T' ||
        Header->Magic[2] != 'E' || Header->Magic[3] != '\0') {
        return MT_INVALID_IMAGE_FORMAT;
    }

    // An image with no imports is valid; it may issue native syscalls itself.
    if (Header->imports_size == 0) return MT_SUCCESS;
    if (Header->imports_size % sizeof(MT_IMPORT_ENTRY) != 0 ||
        !LdrpImageRangeValid(
            Header->imports_rva,
            Header->imports_size,
            ImageSize
        )) {
        return MT_INVALID_IMAGE_FORMAT;
    }

    // Point to import table.
    MT_IMPORT_ENTRY* ImportTable = (MT_IMPORT_ENTRY*)(ImageBase + Header->imports_rva);
    size_t ImportCount = Header->imports_size / sizeof(MT_IMPORT_ENTRY);
    printf(COLOR_RED, "**In MTDLL Resolve imports - ImportCount %lu**\n", ImportCount);

    // Iterate over imports
    for (size_t i = 0; i < ImportCount; i++)
    {
        MT_IMPORT_ENTRY* Entry = &ImportTable[i];

        const char* LibName = LdrpImageString(
            ImageBase,
            ImageSize,
            Entry->lib_name_rva
        );
        const char* FuncName = LdrpImageString(
            ImageBase,
            ImageSize,
            Entry->func_name_rva
        );
        if (!LibName || !FuncName ||
            !LdrpImageRangeValid(
                Entry->iat_addr_rva,
                sizeof(void*),
                ImageSize
            )) {
            return MT_INVALID_IMAGE_FORMAT;
        }

        // The address of the IAT to patch to new function ptr.
        void** IatSlot = (void**)(ImageBase + Entry->iat_addr_rva);
        PLDR_DATA_TABLE_ENTRY ImportedEntry = NULL;

        Status = LdrpReferenceDependency(ExecutableEntry, LibName, &ImportedEntry);

        if (MT_FAILURE(Status)) return Status;

        // Call the final resolver
        Status = LdrpGetProcedureAddress(
            ImportedEntry,
            FuncName,
            IatSlot
        );

        if (MT_FAILURE(Status)) return Status;
    }

    return MT_SUCCESS;
}

MTDLL_API
void
LdrInitializeProcess(
    IN PPEB InitialPeb,
    IN PTEB InitialTeb,
    IN uint64_t EntryPoint,
    IN PMTDLL_BASIC_TYPES BasicTypes
)

/*++

    Routine description:

        Initializes the process GS base, process heap, loader entries, and
        imported function addresses before entering the executable.

    Arguments:

        [IN] InitialPeb - The process environment block to initialize.
        [IN] InitialTeb - The initial thread environment block.
        [IN] EntryPoint - The executable entry point.
        [IN] BasicTypes - Loader metadata for the executable and MTDLL images.

    Return Values:

        None. Initialization failures request termination of the current
        process before normal entry-point execution.

--*/

{
    // Initialize GS base IMMEDIATELY so NtCurrentTeb() and SetLastError() work.
    __asm__ volatile (
        "wrgsbase %0"
        :
    : "r"(InitialTeb)
        : "memory"
        );

    InitialTeb->ProcessEnvironmentBlock = InitialPeb;

    // Create the process heap
    InitialPeb->ProcessHeap = HeapCreate(HEAP_CREATE_NONE, 0, 0);

    if (!InitialPeb->ProcessHeap) {
        // Creating initial heap failure.
        MtTerminateProcess(MtCurrentProcess(), GetLastStatus());
    }

    // Create the loader lock mutex
    InitialPeb->LoaderData.LoaderLock = CreateMutex(false, NULL);

    if (InitialPeb->LoaderData.LoaderLock == MT_INVALID_HANDLE) {
        MtTerminateProcess(
            MtCurrentProcess(),
            GetLastStatus()
        );
    }

    // Set initial PEB LoaderData to be our process.
    PLDR_DATA_TABLE_ENTRY ProcessEntry = (PLDR_DATA_TABLE_ENTRY)HeapAlloc(GetProcessHeap(), HEAP_ALLOCATE_ZERO_MEMORY, sizeof(LDR_DATA_TABLE_ENTRY));

    if (!ProcessEntry) {
        // Allocation failure, we terminate process.
        MtTerminateProcess(MtCurrentProcess(), GetLastStatus());
    }

    // Initialize the PEB list entry.
    InitializeListHead(&InitialPeb->LoaderData.LoadedModuleList);

    // Set fields (process)
    ProcessEntry->Base = BasicTypes->PrimaryExecutable.Base;
    ProcessEntry->ReferenceCount = 1;
    ProcessEntry->Pinned = true;
    ProcessEntry->State = LdrModuleLoading;
    ProcessEntry->EntryPoint = (void*)EntryPoint;
    strncpy(ProcessEntry->FullName, BasicTypes->PrimaryExecutable.FullPath, sizeof(ProcessEntry->FullName));
    ProcessEntry->LoadTime = BasicTypes->EpochCreation;
    ProcessEntry->SizeOfImage = BasicTypes->PrimaryExecutable.Size;
    InitializeListHead(&ProcessEntry->DependencyListHead);
    InitializeListHead(&ProcessEntry->LoadedModuleList);

    // Insert into PEB.
    InsertHeadList(&InitialPeb->LoaderData.LoadedModuleList, &ProcessEntry->LoadedModuleList);

    // Now add mtdll into the PEB as well.
    PLDR_DATA_TABLE_ENTRY MtdllEntry = (PLDR_DATA_TABLE_ENTRY)HeapAlloc(GetProcessHeap(), HEAP_ALLOCATE_ZERO_MEMORY, sizeof(LDR_DATA_TABLE_ENTRY));
    if (!MtdllEntry) {
        MtTerminateProcess(MtCurrentProcess(), GetLastError());
    }

    // Set fields for mtdll.
    MtdllEntry->Base = BasicTypes->Mtdll.Base;
    MtdllEntry->ReferenceCount = 1;
    MtdllEntry->Pinned = true;
    MtdllEntry->State = LdrModuleLoaded;
    MtdllEntry->EntryPoint = NULL; // MTDLL Does not have an Entrypoint.
    strncpy(MtdllEntry->FullName, BasicTypes->Mtdll.FullPath, sizeof(MtdllEntry->FullName));
    MtdllEntry->LoadTime = BasicTypes->EpochCreation;
    MtdllEntry->SizeOfImage = BasicTypes->Mtdll.Size;
    InitializeListHead(&MtdllEntry->DependencyListHead);
    InitializeListHead(&MtdllEntry->LoadedModuleList);

    // Insert into PEB.
    InsertTailList(&InitialPeb->LoaderData.LoadedModuleList, &MtdllEntry->LoadedModuleList);

    // Resolve its imports.
    MTSTATUS Status = LdrpProcessImports(ProcessEntry, InitialPeb);

    // In Windows when an Import fails it usually creates a MessageBox first to notify the user. (only for when the main executable imports that is)
    // But we dont have that yet! :(
    if (MT_FAILURE(Status)) MtTerminateProcess(MtCurrentProcess(), Status);

    // Set our process as loaded now
    ProcessEntry->State = LdrModuleLoaded;

    // Initialize the thread now.
    // TODO, Change NULL to argc and argv.
    // To be honest, I never used argc and argv in my life :)
    LdrInitializeThread(InitialTeb, InitialPeb, EntryPoint, 0);
}
