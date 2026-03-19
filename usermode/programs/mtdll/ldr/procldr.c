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
#include "../includes/mteheader.h"


// 1. LDR_DATA_TABLE_ENTRY of Dll.
// 2. "WriteFile"
// 3. The address of the replaceable pointer in .data

static
MTSTATUS
LdrpResolveImport(
    IN PLDR_DATA_TABLE_ENTRY DllEntry,
    IN const char* FunctionName,
    OUT void** IatSlotPointer
)

// Shouldnt we use an array of function names and IatSlots so we can fill them in faster instead of function calling each iteration?

{
    // Grab image base.
    uint8_t* ImageBase = (uint8_t*)DllEntry->Base;

    // Use the export table of the DllEntry given to load into the Iat.
    MTE_HEADER* Header = (MTE_HEADER*)ImageBase;

    // Validate we have exports.
    if (Header->exports_size == 0) return MT_NOT_FOUND;

    // Iterate over the export table to find the required export for the import.
    MT_EXPORT_ENTRY* ExportTable = (MT_EXPORT_ENTRY*)(ImageBase + Header->exports_rva);
    size_t ExportCount = Header->exports_size / sizeof(MT_EXPORT_ENTRY);

    // Iterate over the imports.
    for (size_t i = 0; i < ExportCount; i++) {
        MT_EXPORT_ENTRY* Entry = &ExportTable[i];

        const char* ExportFunctionName = (ImageBase + Entry->name_rva);
        const void* ExportFunctionAddress = (ImageBase + Entry->func_rva);

        // If this is the function that the import required, we now use it.
        if (strcmp(FunctionName, ExportFunctionName) == 0) {
            // This is it! Replace ptr.
            *IatSlotPointer = ExportFunctionAddress;
            return MT_SUCCESS;
        }
    }

    // Couldn't find the function..
    return MT_NOT_FOUND;
}

static bool GetBaseName(const char* fullpath, char* out, size_t outsz) {
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

static
PLDR_DATA_TABLE_ENTRY
LdrFindEntryForModule(
    IN const char* ModuleName,
    IN PPEB Peb
)

{
    // We iterate over the PEB and see if we found it.
    // I took the PebPointer as an argument since we cant do Teb->Peb because we might be at a point where we havent setupped the main thread yet.
    PDOUBLY_LINKED_LIST ListHead = &Peb->LoaderData.LoadedModuleList;
    PDOUBLY_LINKED_LIST Curr = ListHead->Flink;

    while (ListHead != Curr) {
        // Get LDR.
        PLDR_DATA_TABLE_ENTRY Entry = CONTAINING_RECORD(Curr, LDR_DATA_TABLE_ENTRY, LoadedModuleList);

        // String compare.
        char ImageName[24];
        if (!GetBaseName(Entry->FullName, ImageName, sizeof(ImageName))) goto AdvancePtr;

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

static
MTSTATUS 
LdrpProcessImports(
    IN PLDR_DATA_TABLE_ENTRY ExecutableEntry,
    IN PPEB PebPointer
)

{
    // Declaration of status (function scope)
    MTSTATUS Status;

    // Get the image base of the executable.
    uint8_t* ImageBase = (uint8_t*)ExecutableEntry->Base;

    // 2. Read the MTE Header (Assumes header is at offset 0)
    MTE_HEADER* Header = (MTE_HEADER*)ImageBase;

    // Validate we have imports
    // We return success since technically the process doesnt have any (it should always, always have though, unless it is using manual syscalls???, malware..)
    if (Header->imports_size == 0) return MT_SUCCESS;

    // Point to import table.
    MT_IMPORT_ENTRY* ImportTable = (MT_IMPORT_ENTRY*)(ImageBase + Header->imports_rva);
    size_t ImportCount = Header->imports_size / sizeof(MT_IMPORT_ENTRY);

    // Iterate over imports
    for (size_t i = 0; i < ImportCount; i++)
    {
        MT_IMPORT_ENTRY* Entry = &ImportTable[i];

        // Imports are RVA.
        const char* LibName = (const char*)(ImageBase + Entry->lib_name_rva);
        const char* FuncName = (const char*)(ImageBase + Entry->func_name_rva);

        // The address of the IAT to patch to new function ptr.
        void** IatSlot = (void**)(ImageBase + Entry->iat_addr_rva);

        // Call the final resolver, if we are messing with another dll that is not Mtdll, we would load library it here now, and use its LDR_DATA_TABLE_ENTRY.
        if (strcmp(LibName, "mtdll.mtdll") == 0) {
            // Grab our own LDR_DATA_TABLE_ENTRY.
            PLDR_DATA_TABLE_ENTRY MtdllEntry = LdrFindEntryForModule("mtdll.mtdll", PebPointer);
            if (!MtdllEntry) return MT_NOT_FOUND;

            Status = LdrpResolveImport(
                MtdllEntry,
                FuncName,
                IatSlot
            );
        }
        else {
            // Todo load library.
            // We shouldnt load library every loop, what if the library is already loaded?
            // Then grab LDR_DATA_TABLE_ENTRY.
            return MT_NOT_IMPLEMENTED;
        }

        if (MT_FAILURE(Status)) return Status;
    }

    return MT_SUCCESS;
}

void
LdrInitializeProcess(
    IN PPEB InitialPeb,
    IN PTEB InitialTeb,
    IN uint64_t EntryPoint,
    IN PMTDLL_BASIC_TYPES BasicTypes
)

{
    // Initialize GS base IMMEDIATELY so NtCurrentTeb() and SetLastError() work.
    __asm__ volatile (
        "wrgsbase %0"
        :
    : "r"(InitialTeb)
        : "memory"
        );

    // Set initial PEB LoaderData to be our process.
    // This is a very bad allocation, since virtual alloc literally takes a page no matter the allocation size, and if we are a byte above a page, another page is consumed
    // We need a heap allocator like the MmAllocatePoolWithTag in the kernel space, ill probably implement RtlAllocateHeap soon enough. (TODO)
    PLDR_DATA_TABLE_ENTRY ProcessEntry = VirtualAlloc(NULL, sizeof(LDR_DATA_TABLE_ENTRY), PAGE_READWRITE);
    if (!ProcessEntry) {
        // Allocation failure, we terminate process with MT_NO_MEMORY.
        MtTerminateProcess(MtCurrentProcess(), MT_NO_MEMORY);
    }

    // Initialize the PEB list entry.
    InitializeListHead(&InitialPeb->LoaderData.LoadedModuleList);

    // Set fields (process)
    ProcessEntry->Base = BasicTypes->PrimaryExecutable.Base;
    ProcessEntry->EntryPoint = (void*)EntryPoint;
    strncpy(ProcessEntry->FullName, BasicTypes->PrimaryExecutable.FullPath, sizeof(ProcessEntry->FullName));
    ProcessEntry->LoadTime = BasicTypes->EpochCreation;
    ProcessEntry->SizeOfImage = BasicTypes->PrimaryExecutable.Size;
    InitializeListHead(&ProcessEntry->LoadedModuleList);

    // Insert into PEB.
    InsertHeadList(&InitialPeb->LoaderData.LoadedModuleList, &ProcessEntry->LoadedModuleList);

    // Now add mtdll into the PEB as well.
    PLDR_DATA_TABLE_ENTRY MtdllEntry = VirtualAlloc(NULL, sizeof(LDR_DATA_TABLE_ENTRY), PAGE_READWRITE);
    if (!MtdllEntry) {
        MtTerminateProcess(MtCurrentProcess(), MT_NO_MEMORY);
    }

    // Set fields for mtdll.
    MtdllEntry->Base = BasicTypes->Mtdll.Base;
    MtdllEntry->EntryPoint = LdrInitializeThread; // Default. (since thread entrypoints are re-used, process entry points are only once per init)
    strncpy(MtdllEntry->FullName, BasicTypes->Mtdll.FullPath, sizeof(MtdllEntry->FullName));
    MtdllEntry->LoadTime = BasicTypes->EpochCreation;
    MtdllEntry->SizeOfImage = BasicTypes->Mtdll.Size;
    InitializeListHead(&MtdllEntry->LoadedModuleList);

    // Insert into PEB.
    InsertTailList(&InitialPeb->LoaderData.LoadedModuleList, &MtdllEntry->LoadedModuleList);

    // Resolve its imports.
    MTSTATUS Status = LdrpProcessImports(ProcessEntry, InitialPeb);

    // In Windows when an Import fails it usually creates a MessageBox first to notify the user.
    // But we dont have that yet! :(
    if (MT_FAILURE(Status)) MtTerminateProcess(MtCurrentProcess(), Status);

    // Initialize the thread now.
    // TODO, Change NULL to argc and argv.
    // To be honest, I never used argc and argv in my life :)
    LdrInitializeThread(InitialTeb, InitialPeb, EntryPoint, 0);
}