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

static
bool
LdrpImageRangeValid(
    IN uint64_t Rva,
    IN uint64_t Size,
    IN uint64_t ImageSize
);

MTSTATUS
LdrpFinalizeImageProtections(
    IN PLDR_DATA_TABLE_ENTRY Module
)

/*++

    Routine description:

        Applies the final page protections to a loaded MTE image after the
        loader has finished relocations, imports and TLS fixups.

    Arguments:

        [IN] Module - Loader entry describing the mapped image.

    Return Values:

        MT_SUCCESS when every image region was protected successfully, or an
        error status when the image layout or protection change is invalid.

    Notes:

        A failure may leave the image partially protected. The caller must
        reject and unmap the image instead of continuing its initialization.

--*/

{
    if (!Module || !Module->Base) return MT_INVALID_PARAM;

    // The image must contain atleast the header.
    if (Module->SizeOfImage < sizeof(MTE_HEADER)) {
        return MT_INVALID_IMAGE_FORMAT;
    }

    // Convert to the MTE header.
    PMTE_HEADER Header = (PMTE_HEADER)Module->Base;
    uint8_t* ByteableHeader = Module->Base;

    // Verify the MTE signature first.
    if (ByteableHeader[0] != 'M' ||
        ByteableHeader[1] != 'T' ||
        ByteableHeader[2] != 'E' ||
        ByteableHeader[3] != '\0') {
        return MT_INVALID_IMAGE_FORMAT;
    }

    // Every boundary must be page aligned so protections cannot share pages.
    if (!MTE_IS_PAGE_ALIGNED(Module->Base) ||
        !MTE_IS_PAGE_ALIGNED(Module->SizeOfImage) ||
        !MTE_IS_PAGE_ALIGNED(Header->TextRVA) ||
        !MTE_IS_PAGE_ALIGNED(Header->DataRVA)) {
        return MT_INVALID_IMAGE_FORMAT;
    }

    // Validate file backed regions before using their ending addresses.
    if (!LdrpImageRangeValid(
            Header->TextRVA,
            Header->TextSize,
            Module->SizeOfImage
        ) ||
        !LdrpImageRangeValid(
            Header->DataRVA,
            Header->DataSize,
            Module->SizeOfImage
        )) {
        return MT_INVALID_IMAGE_FORMAT;
    }

    // ALIGN_UP adds the page mask, make sure BSS size cannot wrap.
    if (Header->BssSize > UINT64_MAX - MTE_PAGE_MASK) {
        return MT_INVALID_IMAGE_FORMAT;
    }

    // BSS has no RVA in MTE, it occupies the final pages of the image.
    uint64_t BssSpan = MTE_ALIGN_UP(Header->BssSize);
    if (BssSpan > Module->SizeOfImage) {
        return MT_INVALID_IMAGE_FORMAT;
    }

    uint64_t BssStart = Module->SizeOfImage - BssSpan;
    uint64_t DataEnd = Header->DataRVA + Header->DataSize;

    // Metadata starts on the first page after initialized data.
    if (DataEnd > UINT64_MAX - MTE_PAGE_MASK) {
        return MT_INVALID_IMAGE_FORMAT;
    }

    uint64_t MetadataStart = MTE_ALIGN_UP(DataEnd);

    // Make sure the sections are ordered exactly like the linker places them.
    if (Header->TextRVA < sizeof(MTE_HEADER) ||
        Header->TextRVA >= Header->DataRVA ||
        Header->TextSize > Header->DataRVA - Header->TextRVA ||
        MetadataStart > BssStart) {
        return MT_INVALID_IMAGE_FORMAT;
    }

    uint64_t HeaderSize = Header->TextRVA;
    uint64_t TextSize = Header->DataRVA - Header->TextRVA;
    uint64_t DataSize = MetadataStart - Header->DataRVA;
    uint64_t MetadataSize = BssStart - MetadataStart;

    // Validate BSS bytes against the complete mapped image.
    if (!LdrpImageRangeValid(
            BssStart,
            Header->BssSize,
            Module->SizeOfImage
        )) {
        return MT_INVALID_IMAGE_FORMAT;
    }

    // Loader directories must reside inside the metadata pages.
    if ((Header->exports_size != 0 &&
         (Header->exports_rva < MetadataStart ||
          Header->exports_rva >= BssStart ||
          Header->exports_size > BssStart - Header->exports_rva)) ||
        (Header->reloc_size != 0 &&
         (Header->reloc_rva < MetadataStart ||
          Header->reloc_rva >= BssStart ||
          Header->reloc_size > BssStart - Header->reloc_rva)) ||
        (Header->imports_size != 0 &&
         (Header->imports_rva < MetadataStart ||
          Header->imports_rva >= BssStart ||
          Header->imports_size > BssStart - Header->imports_rva)) ||
        Header->exports_size % sizeof(MT_EXPORT_ENTRY) != 0 ||
        Header->reloc_size % sizeof(MTE_RELOCATION) != 0 ||
        Header->imports_size % sizeof(MT_IMPORT_ENTRY) != 0) {
        return MT_INVALID_IMAGE_FORMAT;
    }

    // TLS directory is metadata too, not another section boundary.
    bool HasTlsRva = Header->tls_rva != 0;
    bool HasTlsSize = Header->tls_size != 0;
    if (HasTlsRva != HasTlsSize ||
        (HasTlsSize &&
         (Header->tls_size != sizeof(MTE_TLS_DIRECTORY) ||
          Header->tls_rva < MetadataStart ||
          Header->tls_rva >= BssStart ||
          Header->tls_size > BssStart - Header->tls_rva))) {
        return MT_INVALID_IMAGE_FORMAT;
    }

    // All regions are validated, change the protections now.
    USER_PROTECTION_TYPE OldProt;
    bool Success = VirtualProtect(
        Module->Base,
        HeaderSize,
        PAGE_READONLY,
        &OldProt
    );

    if (!Success) {
        return GetLastStatus();
    }

    // Text and read only data can execute but cannot be written.
    Success = VirtualProtect(
        (void*)((uintptr_t)Module->Base + Header->TextRVA),
        TextSize,
        PAGE_EXECUTE_READ,
        &OldProt
    );

    if (!Success) {
        return GetLastStatus();
    }

    // Data can be written but cannot execute. It may be empty.
    if (DataSize != 0) {
        Success = VirtualProtect(
            (void*)((uintptr_t)Module->Base + Header->DataRVA),
            DataSize,
            PAGE_READWRITE,
            &OldProt
        );

        if (!Success) {
            return GetLastStatus();
        }
    }

    // Loader metadata can only be read after fixups are complete.
    if (MetadataSize != 0) {
        Success = VirtualProtect(
            (void*)((uintptr_t)Module->Base + MetadataStart),
            MetadataSize,
            PAGE_READONLY,
            &OldProt
        );

        if (!Success) {
            return GetLastStatus();
        }
    }

    // BSS is zero filled writable data and cannot execute. It may be empty.
    if (BssSpan != 0) {
        Success = VirtualProtect(
            (void*)((uintptr_t)Module->Base + BssStart),
            BssSpan,
            PAGE_READWRITE,
            &OldProt
        );

        if (!Success) {
            return GetLastStatus();
        }
    }

    // All image pages now have their final protections.
    return MT_SUCCESS;
}

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

static
MTSTATUS
LdrpInitializeProcessArguments(
    IN OUT PMT_PROCESS_PARAMETERS Parameters
)

{
    // Validate the kernel supplied process parameters
    if (!Parameters || Parameters->Size < sizeof(MT_PROCESS_PARAMETERS) || !Parameters->CommandLine) {
        return MT_INVALID_PARAM;
    }
    
    // Start parsing the command line.
    size_t Index = 0;
    size_t ArgumentStringBytes = 0;
    int32_t argc = 0;
    bool InQuotes = false;
    bool ArgumentStarted = false;
    size_t Boundary = Parameters->CommandLineLength;
    char* CmdLine = Parameters->CommandLine;

    if (Parameters->CommandLine[Boundary] != '\0') {
        return MT_INVALID_PARAM;
    }

    while (Index < Boundary) {
        // We are inside of the arguments themselves, we must do some careful considerations
        // Check if we are in quotes, if we are, a space does not end the current string parse (and so does not increment argc)
        // an example is: program.mtexe first "second argument" ""
        // If we would have parsed the space inside of the quotes it would have treated "second argument" as "second (ARGC++ HAPPENED) argument"
        if (CmdLine[Index] == '"') {
            InQuotes = !InQuotes;
            ArgumentStarted = true;
            Index++;
            continue;
        }

        if (CmdLine[Index] == ' ' && !InQuotes) {
            // Found a space which isnt inside of a quote
            // But if an argument has started (start of the str)
            // If we are not in an argument (inside of reoccuring spaces) then skip
            // example is program.mtexe arg1    arg2 (once we reach arg2 ArgumentStarted would be true)

            if (ArgumentStarted) {
                // Found the space after this argument, stop treating the next character as an argument
                // and keep scanning
                ArgumentStringBytes++;
                argc++;
                ArgumentStarted = false;
            }

            Index++;
            continue;
        }

        ArgumentStarted = true;
        ArgumentStringBytes++;
        Index++;
    }

    // Finished parsing the string
    if (InQuotes) {
        // Commandline finished with an opened quote, that is disallowed
        // we would never know when to end it.
        return MT_INVALID_PARAM;
    }

    if (ArgumentStarted) {
        // Include null terminator too
        // program.mtexe first
        ArgumentStringBytes++; // Null terminator for first
        argc++; // Include first as an argument
    }
    
    // Allocate argument vector now based on the amount of bytes we need
    // and concat the strings with a nullterm at each of them so argv[0] can be read without going over the index
    size_t PointerCount = (size_t)argc + 1; // Include + 1 because argv[lastindex] should be == NULL immediately (aka nulltermed)

    // Anti overflow
    if (PointerCount > SIZE_MAX / sizeof(char*)) {
        return MT_INVALID_PARAM;
    }

    size_t PointerBytes = PointerCount * sizeof(char*);

    if (PointerBytes > SIZE_MAX - ArgumentStringBytes) {
        return MT_INVALID_PARAM;
    }

    // The allocation size is the size of the pointers PLUS
    // the argument strings size themselves
    size_t AllocationSize = PointerBytes + ArgumentStringBytes;

    // Alloc
    char** ArgVector = HeapAlloc(
        GetProcessHeap(),
        HEAP_ALLOCATE_ZERO_MEMORY,
        AllocationSize
    );

    if (!ArgVector) {
        return GetLastStatus();
    }

    // Skip over the argv pointers and start at the strings
    char* StringCursor = (char*)ArgVector + PointerBytes;

    // Copy each string argument into the string cursor and nullterm each of them at the end
    // Basically the same loop but now we are copying the strings over
    Index = 0;
    size_t ArgVectorIndex = 0;
    ArgumentStarted = false;
    InQuotes = false;

    // Index advances through source characters, while StringCursor advances only when an output byte is written.
    while (Index < Boundary) {
        char Character = CmdLine[Index];

        if (Character == '"') {
            if (!ArgumentStarted) {
                ArgVector[ArgVectorIndex] = StringCursor;
                ArgumentStarted = true;
            }

            InQuotes = !InQuotes;
            Index++;
            continue;
        }

        if (Character == ' ' && !InQuotes) {
            if (ArgumentStarted) {
                *StringCursor++ = '\0';
                ArgVectorIndex++;
                ArgumentStarted = false;
            }

            Index++;
            continue;
        }

        if (!ArgumentStarted) {
            ArgVector[ArgVectorIndex] = StringCursor;
            ArgumentStarted = true;
        }

        *StringCursor++ = Character;
        Index++;
    }

    if (ArgumentStarted) {
        // Include last string argument
        *StringCursor++ = '\0';
        ArgVectorIndex++;
    }

    if (ArgVectorIndex != (size_t)argc || StringCursor != (char*)ArgVector + AllocationSize) {
        // ArgC should be the same as the ArgV index
        // And the string cursor should be at the exact end of the allocated memory chunk
        // If not, free and return error.
        HeapFree(
            GetProcessHeap(),
            HEAP_FREE_NO_OPTIONS,
            ArgVector
        );

        return MT_INVALID_STATE;
    }

    // Set argument vector and count and return success.
    // Set argc index as NULL since thats how the standard is.
    ArgVector[argc] = NULL;
    Parameters->ArgumentCount = argc;
    Parameters->ArgumentVector = ArgVector;
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

    // Initialize ARGC and ARGV for the process.
    MTSTATUS Status = LdrpInitializeProcessArguments(
        InitialPeb->ProcessParameters
    );

    if (MT_FAILURE(Status)) {
        MtTerminateProcess(
            MtCurrentProcess(),
            Status
        );
    }

    // Create the loader lock mutex
    InitialPeb->LoaderData.LoaderLock = CreateMutex(false, NULL);
    InitialPeb->NextTlsIndex = 0;

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
    ProcessEntry->TlsIndex = MT_INVALID_TLS_INDEX;
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
    MtdllEntry->TlsIndex = MT_INVALID_TLS_INDEX;
    InitializeListHead(&MtdllEntry->DependencyListHead);
    InitializeListHead(&MtdllEntry->LoadedModuleList);

    // Insert into PEB.
    InsertTailList(&InitialPeb->LoaderData.LoadedModuleList, &MtdllEntry->LoadedModuleList);

    // Acquire the loader lock and do fixups for both the EXE and MTDLL
    uint32_t LoaderWait = WaitForSingleObject(
        InitialPeb->LoaderData.LoaderLock,
        MT_INFINITE
    );

    if (LoaderWait != WAIT_OBJECT_0) {
        MtTerminateProcess(
            MtCurrentProcess(),
            GetLastStatus()
        );
    }

    Status = LdrRegisterModuleTlsLocked(InitialPeb, ProcessEntry);


    if (MT_SUCCEEDED(Status)) {
        Status = LdrRegisterModuleTlsLocked(
            InitialPeb,
            MtdllEntry
        );
    }

    ReleaseMutex(InitialPeb->LoaderData.LoaderLock);

    // If one of the statuses failed after releasing the lock
    // terminate the program.
    if (MT_FAILURE(Status)) {
        MtTerminateProcess(MtCurrentProcess(), Status);
    }

    // Resolve its imports.
    Status = LdrpProcessImports(ProcessEntry, InitialPeb);

    // In Windows when an Import fails it usually creates a MessageBox first to notify the user. (only for when the main executable imports that is)
    // But we dont have that yet! :(
    if (MT_FAILURE(Status)) MtTerminateProcess(MtCurrentProcess(), Status);

    // Imports and TLS fixups are complete, apply final image protections.
    Status = LdrpFinalizeImageProtections(MtdllEntry);
    if (MT_FAILURE(Status)) {
        MtTerminateProcess(MtCurrentProcess(), Status);
    }

    Status = LdrpFinalizeImageProtections(ProcessEntry);
    if (MT_FAILURE(Status)) {
        MtTerminateProcess(MtCurrentProcess(), Status);
    }

    // Set our process as loaded now
    ProcessEntry->State = LdrModuleLoaded;

    // Initialize the thread now.
    LdrInitializeThread(InitialTeb, InitialPeb, EntryPoint, (uintptr_t)InitialPeb->ProcessParameters);
}
