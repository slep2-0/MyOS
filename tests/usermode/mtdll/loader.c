#include "mtdll.h"
#include "errorhandlingapi.h"

#include <mte.h>

#include "loader_fixture.h"
#include "loader_test.h"

typedef uint32_t (*PLOADER_TEST_QUERY_STATE)(void);

typedef struct _MTP_LOADER_PROTECTION_EXPECTATION {
    void* Address;
    uintptr_t Operation;
    uint32_t FilterCount;
    bool Valid;
} MTP_LOADER_PROTECTION_EXPECTATION;

static volatile uint32_t MtpLoaderAttachCount;
static volatile uint32_t MtpLoaderDetachCount;

static int
MtpLoaderProtectionFilter(
    IN PEXCEPTION_POINTERS Information,
    IN OUT MTP_LOADER_PROTECTION_EXPECTATION* Expectation
)

/*++

    Routine description:

        Validates an access violation raised by an image protection probe.

    Arguments:

        [IN] Information - Exception information supplied to the filter.
        [IN, OUT] Expectation - Expected fault address and operation.

    Return Values:

        MT_EXCEPTION_EXECUTE_HANDLER so the protection test can continue.

--*/

{
    Expectation->FilterCount++;
    Expectation->Valid =
        Information != NULL &&
        Information->ExceptionRecord != NULL &&
        Information->ContextRecord != NULL &&
        Information->ExceptionRecord->ExceptionCode ==
            (uint32_t)MT_ACCESS_VIOLATION &&
        Information->ExceptionRecord->NumberParameters == 2 &&
        Information->ExceptionRecord->ExceptionInformation[0] ==
            Expectation->Operation &&
        Information->ExceptionRecord->ExceptionInformation[1] ==
            (uintptr_t)Expectation->Address;

    return MT_EXCEPTION_EXECUTE_HANDLER;
}

static bool
MtpLoaderExpectWriteFault(
    IN void* Address
)

/*++

    Routine description:

        Verifies that writing to an image address raises an access violation.

    Arguments:

        [IN] Address - Readable image address which must not be writable.

    Return Values:

        true when the expected write fault was handled, or false otherwise.

--*/

{
    volatile uint8_t* Target = (volatile uint8_t*)Address;
    uint8_t Original = *Target;
    volatile bool WriteContinued = false;
    volatile bool HandlerRan = false;
    MTP_LOADER_PROTECTION_EXPECTATION Expectation = {
        .Address = Address,
        .Operation = 1
    };

    __try {
        // Write the same byte so a missing protection cannot corrupt the image.
        *Target = Original;
        WriteContinued = true;
    }
    __except (
        MtpLoaderProtectionFilter(
            GetExceptionInformation(),
            &Expectation
        )
    ) {
        HandlerRan = true;
    }

    return !WriteContinued && HandlerRan &&
        Expectation.FilterCount == 1 && Expectation.Valid;
}

static bool
MtpLoaderExpectExecuteFault(
    IN void* Address
)

/*++

    Routine description:

        Verifies that executing from writable image data raises an access
        violation.

    Arguments:

        [IN] Address - Writable image address which must not be executable.

    Return Values:

        true when data remained writable and execution was rejected, or false
        otherwise.

--*/

{
    volatile uint8_t* Target = (volatile uint8_t*)Address;
    uint8_t Original = *Target;
    volatile bool ExecuteContinued = false;
    volatile bool HandlerRan = false;
    MTP_LOADER_PROTECTION_EXPECTATION Expectation = {
        .Address = Address,
        .Operation = 8
    };

    // A RET makes an unexpectedly executable data page return safely.
    *Target = 0xC3;

    __try {
        ((void (*)(void))Address)();
        ExecuteContinued = true;
    }
    __except (
        MtpLoaderProtectionFilter(
            GetExceptionInformation(),
            &Expectation
        )
    ) {
        HandlerRan = true;
    }

    // Restore the byte after either the expected fault or an unexpected call.
    *Target = Original;

    return !ExecuteContinued && HandlerRan &&
        Expectation.FilterCount == 1 && Expectation.Valid;
}

static bool
MtpLoaderCheckProtection(
    IN void* Address,
    IN USER_PROTECTION_TYPE ExpectedProtection
)

/*++

    Routine description:

        Verifies the published protection of one image address.

    Arguments:

        [IN] Address - Address whose virtual memory region is queried.
        [IN] ExpectedProtection - Protection required for the region.

    Return Values:

        true when the query reports the expected protection, or false
        otherwise.

--*/

{
    MEMORY_BASIC_INFORMATION Information;
    return VirtualQuery(Address, &Information) &&
        Information.Protection == ExpectedProtection &&
        (uintptr_t)Address >= (uintptr_t)Information.BaseAddress &&
        (uintptr_t)Address - (uintptr_t)Information.BaseAddress <
            Information.RegionSize;
}

static bool
MtpLoaderVerifyModuleProtections(
    IN PLDR_DATA_TABLE_ENTRY Module
)

/*++

    Routine description:

        Verifies the final text, data, and metadata protections of one loaded
        MTE image.

    Arguments:

        [IN] Module - Loaded module whose runtime protections are tested.

    Return Values:

        true when every protection boundary behaves correctly, or false when
        the image layout or a protection check fails.

--*/

{
    if (!Module || !Module->Base ||
        Module->SizeOfImage < sizeof(MTE_HEADER)) {
        return false;
    }

    PMTE_HEADER Header = (PMTE_HEADER)Module->Base;
    if (Header->Magic[0] != 'M' || Header->Magic[1] != 'T' ||
        Header->Magic[2] != 'E' || Header->Magic[3] != '\0' ||
        Header->DataRVA > UINT64_MAX - Header->DataSize) {
        return false;
    }

    uint64_t DataEnd = Header->DataRVA + Header->DataSize;
    if (DataEnd > UINT64_MAX - MTE_PAGE_MASK ||
        Header->BssSize > UINT64_MAX - MTE_PAGE_MASK) {
        return false;
    }

    uint64_t MetadataStart = MTE_ALIGN_UP(DataEnd);
    uint64_t BssSpan = MTE_ALIGN_UP(Header->BssSize);
    if (BssSpan > Module->SizeOfImage) {
        return false;
    }

    uint64_t BssStart = Module->SizeOfImage - BssSpan;
    if (Header->TextRVA >= Header->DataRVA ||
        Header->DataRVA > MetadataStart ||
        MetadataStart >= BssStart) {
        return false;
    }

    uint8_t* Base = (uint8_t*)Module->Base;
    void* TextAddress = Base + Header->TextRVA;
    void* MetadataAddress = Base + MetadataStart;

    // An image may have only zero filled writable data.
    bool HasInitializedData = Header->DataRVA < MetadataStart;
    void* WritableAddress = HasInitializedData
        ? Base + Header->DataRVA
        : (BssSpan != 0 ? Base + BssStart : NULL);
    if (!WritableAddress) {
        return false;
    }

    // Verify the complete final region map first.
    if (!MtpLoaderCheckProtection(Base, PAGE_READONLY) ||
        !MtpLoaderCheckProtection(TextAddress, PAGE_EXECUTE_READ) ||
        !MtpLoaderCheckProtection(MetadataAddress, PAGE_READONLY) ||
        (HasInitializedData &&
         !MtpLoaderCheckProtection(Base + Header->DataRVA, PAGE_READWRITE)) ||
        (BssSpan != 0 &&
         !MtpLoaderCheckProtection(Base + BssStart, PAGE_READWRITE))) {
        return false;
    }

    // Confirm read only pages fault and writable storage remains NX.
    return MtpLoaderExpectWriteFault(Base) &&
        MtpLoaderExpectWriteFault(TextAddress) &&
        MtpLoaderExpectWriteFault(MetadataAddress) &&
        MtpLoaderExpectExecuteFault(WritableAddress);
}

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

    PLDR_DATA_TABLE_ENTRY MtdllEntry = LdrFindEntryForModule(
        "mtdll.mtdll",
        MtCurrentPeb(),
        false
    );
    if (!MtdllEntry || !MtdllEntry->Pinned ||
        MtdllEntry->ReferenceCount == 0) {
        return MT_LOADER_TEST_DEPENDENCY_REFERENCE;
    }
    uint32_t MtdllBaselineReferences = MtdllEntry->ReferenceCount;

    PDOUBLY_LINKED_LIST ModuleList =
        &MtCurrentPeb()->LoaderData.LoadedModuleList;
    if (ModuleList->Flink == ModuleList) {
        return MT_LOADER_TEST_PROCESS_PROTECTION;
    }

    PLDR_DATA_TABLE_ENTRY ProcessEntry = CONTAINING_RECORD(
        ModuleList->Flink,
        LDR_DATA_TABLE_ENTRY,
        LoadedModuleList
    );
    if (ProcessEntry->Base != ProcessModule ||
        !MtpLoaderVerifyModuleProtections(ProcessEntry)) {
        return MT_LOADER_TEST_PROCESS_PROTECTION;
    }
    if (!MtpLoaderVerifyModuleProtections(MtdllEntry)) {
        return MT_LOADER_TEST_MTDLL_PROTECTION;
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
    if (MtdllEntry->ReferenceCount != MtdllBaselineReferences + 1) {
        return MT_LOADER_TEST_DEPENDENCY_REFERENCE;
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
    if (!MtpLoaderVerifyModuleProtections(GoodEntry)) {
        return MT_LOADER_TEST_DLL_PROTECTION;
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
    if (MtdllEntry->ReferenceCount != MtdllBaselineReferences + 1) {
        return MT_LOADER_TEST_DEPENDENCY_REFERENCE;
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
        if (MtdllEntry->ReferenceCount != MtdllBaselineReferences + 1) {
            return MT_LOADER_TEST_DEPENDENCY_REFERENCE;
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
    if (MtdllEntry->ReferenceCount != MtdllBaselineReferences + 1) {
        return MT_LOADER_TEST_DEPENDENCY_REFERENCE;
    }

    if (!FreeLibrary(GoodModule) ||
        GetLastStatus() != MT_SUCCESS ||
        GetLastError() != ERROR_SUCCESS ||
        MtpLoaderDetachCount != 1) {
        return MT_LOADER_TEST_DETACH;
    }
    if (MtdllEntry->ReferenceCount != MtdllBaselineReferences) {
        return MT_LOADER_TEST_DEPENDENCY_REFERENCE;
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
        MtpLoaderAttachCount != 2 ||
        MtdllEntry->ReferenceCount != MtdllBaselineReferences + 1) {
        return MT_LOADER_TEST_RELOAD;
    }
    if (!FreeLibrary(ReloadedModule) ||
        MtpLoaderDetachCount != 2 ||
        MtdllEntry->ReferenceCount != MtdllBaselineReferences) {
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
        MtdllEntry->ReferenceCount != MtdllBaselineReferences ||
        LdrFindEntryForModule(
            "loaderGood.mtdll",
            MtCurrentPeb(),
            false
        ) != NULL) {
        return MT_LOADER_TEST_CONCURRENT;
    }

    return MT_SUCCESS;
}
