#include "../includes/mtdll.h"
#include "../includes/errorhandlingapi.h"

#include <mte.h>

#include "loader_fixture.h"
#include "loader_test.h"

typedef uint32_t (*PLOADER_TEST_QUERY_STATE)(void);

MTDLL_API
MTSTATUS
MtpRunLoaderTests(
    void
)
{
    if (LoadLibrary(NULL) != NULL ||
        GetLastStatus() != MT_INVALID_PARAM ||
        GetLastError() != ERROR_INVALID_PARAMETER) {
        return MT_LOADER_TEST_INVALID_PARAMETER;
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

    return MT_SUCCESS;
}
