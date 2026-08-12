#include "mtdll.h"
#include "errorhandlingapi.h"

#include <mte.h>

#include "loader_fixture.h"
#include "loader_test.h"

typedef uint32_t (*PLOADER_TEST_QUERY_STATE)(void);

static volatile uint32_t MtpLoaderAttachCount;
static volatile uint32_t MtpLoaderDetachCount;

MTDLL_API
void
MtpLoaderTestRecordReason(
    IN DLL_REASON Reason
)

/*++

    Routine description:

        Records a loader notification reason in the shared test fixture.

    Arguments:

        [IN] Reason - Loader notification or failure reason.

    Return Values:

        None.

--*/

{
    // DllMain calls this while the loader lock serializes notifications.
    if (Reason == DLL_PROCESS_ATTACH) {
        MtpLoaderAttachCount++;
    }
    else if (Reason == DLL_PROCESS_DETACH) {
        MtpLoaderDetachCount++;
    }
}

static uint32_t
MtpLoaderConcurrentWorker(
    IN void* Parameter
)

/*++

    Routine description:

        Runs the loader concurrent worker entry point.

    Arguments:

        [IN] Parameter - Context supplied when the worker thread was created.

    Return Values:

        MT_SUCCESS on success, or MT_LOADER_TEST_CONCURRENT when the concurrent load check fails.

--*/

{
    uint32_t Iterations = (uint32_t)(uintptr_t)Parameter;

    for (uint32_t Index = 0; Index < Iterations; Index++) {
        HMODULE Module = LoadLibrary("loaderGood.mtdll");
        if (!Module) {
            return MT_LOADER_TEST_CONCURRENT;
        }

        PLOADER_TEST_QUERY_STATE QueryState =
            (PLOADER_TEST_QUERY_STATE)GetProcAddress(
                Module,
                "LoaderTestQueryState"
            );
        if (!QueryState || QueryState() != LOADER_TEST_QUERY_RESULT) {
            FreeLibrary(Module);
            return MT_LOADER_TEST_CONCURRENT;
        }

        if (!FreeLibrary(Module)) {
            return MT_LOADER_TEST_CONCURRENT;
        }
    }

    return MT_SUCCESS;
}

MTDLL_API
MTSTATUS
MtpRunLoaderTests(
    void
)

/*++

    Routine description:

        Runs the complete user-mode dynamic-loader test suite.

    Arguments:

        None.

    Return Values:

        MT_SUCCESS when the test passes, or a failure status identifying the violated invariant.

--*/

{
    if (LoadLibrary(NULL) != NULL ||
        GetLastStatus() != MT_INVALID_PARAM ||
        GetLastError() != ERROR_INVALID_PARAMETER) {
        return MT_LOADER_TEST_INVALID_PARAMETER;
    }

    HMODULE ProcessModule = GetModuleHandle(NULL);
    HMODULE MtdllModule = GetModuleHandle("mtdll.mtdll");
    if (!ProcessModule || !MtdllModule) {
        return MT_LOADER_TEST_PINNED;
    }
    if (FreeLibrary(ProcessModule) ||
        GetLastStatus() != MT_ACCESS_DENIED ||
        GetLastError() != ERROR_ACCESS_DENIED ||
        FreeLibrary(MtdllModule) ||
        GetLastStatus() != MT_ACCESS_DENIED ||
        GetLastError() != ERROR_ACCESS_DENIED) {
        return MT_LOADER_TEST_PINNED;
    }

    HMODULE GoodModule = LoadLibrary("loaderGood.mtdll");
    if (!GoodModule) {
        return MT_LOADER_TEST_LOAD_FAILED;
    }
    if (GetLastStatus() != MT_SUCCESS) {
        return MT_LOADER_TEST_LAST_STATUS;
    }
    if (GetLastError() != ERROR_SUCCESS) {
        return MT_LOADER_TEST_LAST_ERROR;
    }
    if (MtpLoaderAttachCount != 1 || MtpLoaderDetachCount != 0) {
        return MT_LOADER_TEST_DETACH;
    }

    PLDR_DATA_TABLE_ENTRY GoodEntry = LdrFindEntryForModule(
        "loaderGood.mtdll",
        MtCurrentPeb(),
        false
    );
    if (!GoodEntry ||
        GoodEntry->State != LdrModuleLoaded ||
        GoodEntry->ReferenceCount != 1 ||
        GoodEntry->Base != GoodModule) {
        return MT_LOADER_TEST_ENTRY_STATE;
    }

    const MTE_HEADER* Header = (const MTE_HEADER*)GoodEntry->Base;
    if ((uintptr_t)GoodEntry->Base == Header->PreferredImageBase) {
        return MT_LOADER_TEST_REBASE;
    }

    if (GetModuleHandle("loaderGood.mtdll") != GoodModule ||
        GetLastStatus() != MT_SUCCESS ||
        GetLastError() != ERROR_SUCCESS) {
        return MT_LOADER_TEST_MODULE_HANDLE;
    }

    if (GetModuleHandle("loaderMissing.mtdll") != NULL ||
        GetLastStatus() != MT_NOT_FOUND ||
        GetLastError() != ERROR_NOT_FOUND) {
        return MT_LOADER_TEST_MISSING_MODULE;
    }

    PLOADER_TEST_QUERY_STATE QueryState =
        (PLOADER_TEST_QUERY_STATE)GetProcAddress(
            GoodModule,
            "LoaderTestQueryState"
        );
    if (!QueryState) {
        return MT_LOADER_TEST_EXPORT;
    }
    if (GetLastStatus() != MT_SUCCESS) {
        return MT_LOADER_TEST_LAST_STATUS;
    }
    if (GetLastError() != ERROR_SUCCESS) {
        return MT_LOADER_TEST_LAST_ERROR;
    }
    if (QueryState() != LOADER_TEST_QUERY_RESULT) {
        return MT_LOADER_TEST_IMPORT_RELOCATION;
    }

    if (GetProcAddress(GoodModule, "LoaderTestMissingExport") != NULL ||
        GetLastStatus() != MT_NOT_FOUND ||
        GetLastError() != ERROR_NOT_FOUND) {
        return MT_LOADER_TEST_MISSING_EXPORT;
    }

    if (GetProcAddress(NULL, "LoaderTestQueryState") != NULL ||
        GetLastStatus() != MT_INVALID_PARAM ||
        GetLastError() != ERROR_INVALID_PARAMETER) {
        return MT_LOADER_TEST_INVALID_PARAMETER;
    }

    HMODULE DuplicateModule = LoadLibrary("loaderGood.mtdll");
    if (DuplicateModule != GoodModule) {
        return MT_LOADER_TEST_DUPLICATE_ENTRY;
    }
    if (GoodEntry->ReferenceCount != 2) {
        return MT_LOADER_TEST_DUPLICATE_REFERENCE;
    }
    if (QueryState() != LOADER_TEST_QUERY_RESULT) {
        return MT_LOADER_TEST_ATTACH_REPEAT;
    }

    for (uint32_t Attempt = 0; Attempt < 2; Attempt++) {
        if (LoadLibrary("loaderFail.mtdll") != NULL ||
            GetLastStatus() != MT_DLL_INITIALIZATION_FAILED) {
            return MT_LOADER_TEST_REJECT_STATUS;
        }
        if (GetLastError() != ERROR_GEN_FAILURE) {
            return MT_LOADER_TEST_LAST_ERROR;
        }
        if (LdrFindEntryForModule(
                "loaderFail.mtdll",
                MtCurrentPeb(),
                false
            ) != NULL) {
            return MT_LOADER_TEST_REJECT_ROLLBACK;
        }
    }

    if (!FreeLibrary(GoodModule) ||
        GetLastStatus() != MT_SUCCESS ||
        GetLastError() != ERROR_SUCCESS ||
        GoodEntry->ReferenceCount != 1 ||
        GoodEntry->State != LdrModuleLoaded ||
        MtpLoaderDetachCount != 0 ||
        QueryState() != LOADER_TEST_QUERY_RESULT) {
        return MT_LOADER_TEST_UNLOAD_REFERENCE;
    }

    if (!FreeLibrary(GoodModule) ||
        GetLastStatus() != MT_SUCCESS ||
        GetLastError() != ERROR_SUCCESS ||
        MtpLoaderDetachCount != 1) {
        return MT_LOADER_TEST_DETACH;
    }
    if (LdrFindEntryForModule(
            "loaderGood.mtdll",
            MtCurrentPeb(),
            false
        ) != NULL ||
        GetModuleHandle("loaderGood.mtdll") != NULL) {
        return MT_LOADER_TEST_UNLOAD_REMOVAL;
    }
    if (FreeLibrary(GoodModule) ||
        GetLastStatus() != MT_NOT_FOUND ||
        GetLastError() != ERROR_NOT_FOUND) {
        return MT_LOADER_TEST_FREE_STATUS;
    }

    HMODULE ReloadedModule = LoadLibrary("loaderGood.mtdll");
    PLOADER_TEST_QUERY_STATE ReloadedQuery =
        (PLOADER_TEST_QUERY_STATE)GetProcAddress(
            ReloadedModule,
            "LoaderTestQueryState"
        );
    if (!ReloadedModule || !ReloadedQuery ||
        ReloadedQuery() != LOADER_TEST_QUERY_RESULT ||
        MtpLoaderAttachCount != 2) {
        return MT_LOADER_TEST_RELOAD;
    }
    if (!FreeLibrary(ReloadedModule) || MtpLoaderDetachCount != 2) {
        return MT_LOADER_TEST_RELOAD;
    }

    HMODULE NoEntryModule = LoadLibrary("NOENTRY.MTE");
    PLDR_DATA_TABLE_ENTRY NoEntry = LdrFindEntryForModule(
        "NOENTRY.MTE",
        MtCurrentPeb(),
        false
    );
    if (!NoEntryModule) {
        return MT_LOADER_TEST_NO_ENTRY_LOAD;
    }
    if (!NoEntry || NoEntry->EntryPoint != NULL) {
        return MT_LOADER_TEST_NO_ENTRY_STATE;
    }
    PLOADER_TEST_QUERY_STATE NoEntryQuery =
        (PLOADER_TEST_QUERY_STATE)GetProcAddress(
            NoEntryModule,
            "LoaderNoEntryQueryState"
        );
    if (!NoEntryQuery || NoEntryQuery() != LOADER_TEST_QUERY_RESULT) {
        return MT_LOADER_TEST_NO_ENTRY_EXPORT;
    }
    if (!FreeLibrary(NoEntryModule)) {
        return MT_LOADER_TEST_NO_ENTRY_FREE;
    }
    if (LdrFindEntryForModule(
            "NOENTRY.MTE",
            MtCurrentPeb(),
            false
        ) != NULL) {
        return MT_LOADER_TEST_NO_ENTRY_REMOVAL;
    }

    enum { WorkerCount = 4, WorkerIterations = 64 };
    HANDLE Workers[WorkerCount];
    uint32_t CreatedWorkers = 0;
    for (; CreatedWorkers < WorkerCount; CreatedWorkers++) {
        Workers[CreatedWorkers] = CreateThread(
            MtpLoaderConcurrentWorker,
            (void*)(uintptr_t)WorkerIterations
        );
        if (Workers[CreatedWorkers] == MT_INVALID_HANDLE) {
            break;
        }
    }

    bool WorkersPassed = CreatedWorkers == WorkerCount;
    for (uint32_t Index = 0; Index < CreatedWorkers; Index++) {
        uint32_t ExitCode = MT_LOADER_TEST_CONCURRENT;
        if (WaitForSingleObject(Workers[Index], MT_INFINITE) != WAIT_OBJECT_0 ||
            !GetExitCodeThread(Workers[Index], &ExitCode) ||
            ExitCode != MT_SUCCESS) {
            WorkersPassed = false;
        }
        if (!CloseHandle(Workers[Index])) {
            WorkersPassed = false;
        }
    }

    if (!WorkersPassed ||
        MtpLoaderAttachCount <= 2 ||
        MtpLoaderAttachCount != MtpLoaderDetachCount ||
        LdrFindEntryForModule(
            "loaderGood.mtdll",
            MtCurrentPeb(),
            false
        ) != NULL) {
        return MT_LOADER_TEST_CONCURRENT;
    }

    return MT_SUCCESS;
}
