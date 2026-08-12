#include "includes/mtdll.h"
#include "includes/errorhandlingapi.h"

void TrimTrailingWhitespace(const char* Str, char Buffer[256])

/*++

    Routine description:

        Removes trailing ASCII whitespace from a mutable string.

    Arguments:

        [IN] Str - String to examine or modify.
        [IN] Buffer - Buffer used to transfer the data.

    Return Values:

        None.

--*/

{
    size_t Length = 0;

    while (Str[Length] && Length < 255) {
        Buffer[Length] = Str[Length];
        Length++;
    }

    while (Length > 0 && isspace(Buffer[Length - 1])) {
        Length--;
    }

    Buffer[Length] = '\0';
}

MTDLL_API
HMODULE
LoadLibrary(
    IN const char* DllPath
)

/*++

    Routine description:

        Loads a named DLL into the current process.

    Arguments:

        [IN] DllPath - Path of the library to load.

    Return Values:

        The loaded module base, or NULL when loading fails.

--*/

{
    if (!DllPath) {
        SetLastStatus(MT_INVALID_PARAM);
        SetLastError(MtStatusToLastError(MT_INVALID_PARAM));
        return NULL;
    }

    // Remove any trailing whitespaces
    char DllPathTrimmed[256];
    TrimTrailingWhitespace(DllPath, DllPathTrimmed);

    // Call internal function
    PLDR_DATA_TABLE_ENTRY DllEntry;
    MTSTATUS Status = LdrLoadDll(DllPathTrimmed, &DllEntry);

    SetLastStatus(Status);
    SetLastError(MtStatusToLastError(Status));

    if (MT_SUCCEEDED(Status)) {
        return DllEntry->Base;
    }
    else {
        return NULL;
    }
}

MTDLL_API HMODULE
GetModuleHandle(
    IN const char* ModuleName
)

/*++

    Routine description:

        Finds the base address of an already loaded module.

    Arguments:

        [IN] ModuleName - Name of the loaded module.

    Return Values:

        The module base for the requested name, or NULL when it is not loaded.

--*/

{
    if (!ModuleName) {
        // Return the current process base address
        // The process is the first entry in the PEB.
        PLDR_DATA_TABLE_ENTRY ProcessEntry = CONTAINING_RECORD(MtCurrentPeb()->LoaderData.LoadedModuleList.Flink, LDR_DATA_TABLE_ENTRY, LoadedModuleList);
        SetLastStatus(MT_SUCCESS);
        SetLastError(MtStatusToLastError(MT_SUCCESS));
        return ProcessEntry->Base;
    }

    // Remove any trailing whitespaces
    char DllPathTrimmed[256];
    TrimTrailingWhitespace(ModuleName, DllPathTrimmed);


    // Acquire the loader mutex
    uint32_t WaitStatus = WaitForSingleObject(MtCurrentPeb()->LoaderData.LoaderLock, MT_INFINITE);

    if (WaitStatus == WAIT_ABANDONED) {
        // Mutex abandonment, do not iterate over the list.
        MTSTATUS Status = GetLastStatus();
        ReleaseMutex(MtCurrentPeb()->LoaderData.LoaderLock);
        SetLastStatus(Status);
        SetLastError(MtStatusToLastError(Status));
        return NULL;
    }

    if (WaitStatus != WAIT_OBJECT_0) {
        // Mutex could not be held.
        MTSTATUS Status = GetLastStatus();
        SetLastStatus(Status);
        SetLastError(MtStatusToLastError(Status));
        return NULL;
    }

    // Acquire the LDR_DATA_TABLE_ENTRY, if the DLL Exists.
    PLDR_DATA_TABLE_ENTRY Entry = LdrFindEntryForModule(DllPathTrimmed, MtCurrentPeb(), false);
    if (!Entry) {
        ReleaseMutex(MtCurrentPeb()->LoaderData.LoaderLock);
        SetLastStatus(MT_NOT_FOUND);
        SetLastError(MtStatusToLastError(MT_NOT_FOUND));
        return NULL;
    }

    ReleaseMutex(MtCurrentPeb()->LoaderData.LoaderLock);
    SetLastStatus(MT_SUCCESS);
    SetLastError(MtStatusToLastError(MT_SUCCESS));
    return Entry->Base;
}

MTDLL_API void*
GetProcAddress(
    IN HMODULE Module,
    IN const char* FunctionName
)

/*++

    Routine description:

        Resolves an exported routine address in a loaded module.

    Arguments:

        [IN] Module - Loaded-module entry affected by the operation.
        [IN] FunctionName - Imported function name to resolve.

    Return Values:

        A pointer to the resulting object or storage, or NULL when no result is available.

--*/

{
    if (!Module || !FunctionName) {
        SetLastStatus(MT_INVALID_PARAM);
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    // Acquire the loader mutex
    uint32_t WaitStatus = WaitForSingleObject(MtCurrentPeb()->LoaderData.LoaderLock, MT_INFINITE);

    if (WaitStatus == WAIT_ABANDONED) {
        // Mutex abandonment, do not iterate over the list.
        MTSTATUS Status = GetLastStatus();
        ReleaseMutex(MtCurrentPeb()->LoaderData.LoaderLock);
        SetLastStatus(Status);
        SetLastError(MtStatusToLastError(Status));
        return NULL;
    }

    if (WaitStatus != WAIT_OBJECT_0) {
        // Mutex could not be held.
        MTSTATUS Status = GetLastStatus();
        SetLastStatus(Status);
        SetLastError(MtStatusToLastError(Status));
        return NULL;
    }

    // Find loaded entry whose the base is given
    PDOUBLY_LINKED_LIST Head = &MtCurrentPeb()->LoaderData.LoadedModuleList;
    PDOUBLY_LINKED_LIST Current = Head->Flink;

    void* FoundProcedure = NULL;
    MTSTATUS Status = MT_NOT_FOUND;

    while (Head != Current) {
        PLDR_DATA_TABLE_ENTRY Entry = CONTAINING_RECORD(Current, LDR_DATA_TABLE_ENTRY, LoadedModuleList);

        if (Entry->Base == Module) {
            // Found the procedure, call internal helper.
            Status = LdrpGetProcedureAddress(
                Entry,
                FunctionName,
                &FoundProcedure
            );
            goto Cleanup;
        }

        Current = Current->Flink;
    }

Cleanup:
    ReleaseMutex(MtCurrentPeb()->LoaderData.LoaderLock);

    SetLastStatus(Status);
    SetLastError(MtStatusToLastError(Status));

    return FoundProcedure;
}

MTDLL_API bool
FreeLibrary(
    IN HMODULE Module
)

/*++

    Routine description:

        Releases a reference acquired for a loaded DLL.

    Arguments:

        [IN] Module - Loaded-module entry affected by the operation.

    Return Values:

        true when the module reference is released, or false on failure.

--*/

{
    // Call internal function, and return status.
    MTSTATUS Status = LdrUnloadDll(Module);
    SetLastStatus(Status);
    SetLastError(MtStatusToLastError(Status));
    return MT_SUCCEEDED(Status);
}