#include <MatanelOS.h>
#include <mtnative.h>
#include <mtstatus.h>

#include "process_test.h"

#define PROCESS_TEST_WORKER_COUNT     4
#define PROCESS_TEST_CHILDREN_PER_RUN 4

static const char ProcessTestCommandLine[] =
    "processChild.mtexe alpha \"two words\" \"\"";
static const char ProcessTestEnvironment[] =
    "PROCESS_TEST=child\0"
    "SECOND=two\0";

static
uint32_t
ProcessTestPriorityWorker(
    IN void* Parameter
)

{
    HANDLE Gate = (HANDLE)(intptr_t)Parameter;
    return WaitForSingleObject(Gate, 30000) == WAIT_OBJECT_0
        ? (uint32_t)MT_SUCCESS
        : (uint32_t)MT_PROCESS_TEST_PRIORITY_WORKER;
}

static
MTSTATUS
ProcessTestThreadPriorityApi(
    void
)

/*++

    Routine description:

        Verifies the public thread-priority wrappers and native validation
        paths against a live worker thread.

    Arguments:

        None.

    Return Values:

        MT_SUCCESS when every priority API check passes, or a process-test
        status identifying the failed check.

--*/

{
    HANDLE Gate = CreateEvent(NotificationEvent, false, NULL);
    if (Gate == MT_INVALID_HANDLE) {
        return MT_PROCESS_TEST_PRIORITY_EVENT;
    }

    HANDLE Thread = CreateThread(
        ProcessTestPriorityWorker,
        (void*)(intptr_t)Gate
    );
    if (Thread == MT_INVALID_HANDLE) {
        return MT_PROCESS_TEST_PRIORITY_THREAD;
    }

    if (GetThreadPriority(Thread) != MT_PRIORITY_NORMAL) {
        return MT_PROCESS_TEST_PRIORITY_QUERY;
    }

    if (!SetThreadPriority(Thread, MT_PRIORITY_HIGHEST_VARIABLE)) {
        return MT_PROCESS_TEST_PRIORITY_SET;
    }
    if (GetThreadPriority(Thread) != MT_PRIORITY_HIGHEST_VARIABLE) {
        return MT_PROCESS_TEST_PRIORITY_QUERY;
    }

    THREAD_BASE_PRIORITY_INFORMATION Information = {
        .BasePriority = MT_PRIORITY_NORMAL
    };
    MTSTATUS Status = MtSetInformationThread(
        Thread,
        (THREADINFOCLASS)UINT32_MAX,
        &Information,
        sizeof(Information)
    );
    if (Status != MT_INVALID_INFO_CLASS) {
        return MT_PROCESS_TEST_PRIORITY_NATIVE_CLASS;
    }

    Status = MtSetInformationThread(
        Thread,
        ThreadBasePriorityInformation,
        &Information,
        0
    );
    if (Status != MT_INFO_LENGTH_MISMATCH) {
        return MT_PROCESS_TEST_PRIORITY_NATIVE_LENGTH;
    }

    const void* InvalidUserPointer =
        (const void*)(MT_HIGHEST_USER_ADDRESS + 1ULL);
    Status = MtSetInformationThread(
        Thread,
        ThreadBasePriorityInformation,
        InvalidUserPointer,
        sizeof(Information)
    );
    if (Status != MT_ACCESS_VIOLATION) {
        return MT_PROCESS_TEST_PRIORITY_NATIVE_POINTER;
    }

    Information.BasePriority = MT_PRIORITY_REALTIME_HIGHEST + 1;
    Status = MtSetInformationThread(
        Thread,
        ThreadBasePriorityInformation,
        &Information,
        sizeof(Information)
    );
    if (Status != MT_INVALID_PARAM) {
        return MT_PROCESS_TEST_PRIORITY_NATIVE_VALUE;
    }

    THREAD_AFFINITY_MASK_INFORMATION AffinityInformation = {
        .AffinityMask = 1
    };
    Status = MtSetInformationThread(
        Thread,
        ThreadAffinityMaskInformation,
        &AffinityInformation,
        sizeof(AffinityInformation)
    );
    if (MT_FAILURE(Status)) {
        return MT_PROCESS_TEST_AFFINITY_SET;
    }

    Status = MtSetInformationThread(
        Thread,
        ThreadAffinityMaskInformation,
        &AffinityInformation,
        0
    );
    if (Status != MT_INFO_LENGTH_MISMATCH) {
        return MT_PROCESS_TEST_AFFINITY_NATIVE_LENGTH;
    }

    AffinityInformation.AffinityMask = 0;
    Status = MtSetInformationThread(
        Thread,
        ThreadAffinityMaskInformation,
        &AffinityInformation,
        sizeof(AffinityInformation)
    );
    if (Status != MT_INVALID_PARAM) {
        return MT_PROCESS_TEST_AFFINITY_NATIVE_VALUE;
    }

    if (!SetEvent(Gate) ||
        WaitForSingleObject(Thread, 30000) != WAIT_OBJECT_0) {
        return MT_PROCESS_TEST_PRIORITY_WORKER;
    }

    uint32_t ExitCode = 0;
    if (!GetExitCodeThread(Thread, &ExitCode) ||
        (MTSTATUS)ExitCode != MT_SUCCESS) {
        return MT_PROCESS_TEST_PRIORITY_WORKER;
    }

    if (!CloseHandle(Thread) || !CloseHandle(Gate)) {
        return MT_PROCESS_TEST_PRIORITY_CLOSE;
    }

    return MT_SUCCESS;
}

static
void
ProcessTestInitializeNativeParameters(
    OUT PMT_CREATE_PROCESS_PARAMETERS Parameters,
    IN const char* ImagePath,
    IN const char* CommandLine
)

/*++

    Routine description:

        Initializes a native process-creation request used by failure tests.

    Arguments:

        [OUT] Parameters - Receives the initialized native request.
        [IN] ImagePath - Executable path placed in the request.
        [IN] CommandLine - Command line placed in the request.

    Return Values:

        None.

--*/

{
    *Parameters = (MT_CREATE_PROCESS_PARAMETERS){ 0 };
    Parameters->Size = sizeof(*Parameters);
    Parameters->ImagePath = ImagePath;
    Parameters->ImagePathLength = strlen(ImagePath);
    Parameters->CommandLine = CommandLine;
    Parameters->CommandLineLength = strlen(CommandLine);
    Parameters->CurrentDirectory = "process-test-dir";
    Parameters->CurrentDirectoryLength = sizeof("process-test-dir") - 1;
    Parameters->Environment = ProcessTestEnvironment;
    Parameters->EnvironmentSize = sizeof(ProcessTestEnvironment);
    Parameters->ParentProcess = MtCurrentProcess();
    Parameters->DesiredAccess = MT_PROCESS_ALL_ACCESS;
}

static
MTSTATUS
ProcessTestNativeFailurePaths(
    void
)

/*++

    Routine description:

        Verifies native pointer rejection and late process-creation rollback.

    Arguments:

        None.

    Return Values:

        MT_SUCCESS when every failure path behaves correctly, or a process-test
        status identifying the failed check.

--*/

{
    MT_CREATE_PROCESS_PARAMETERS Parameters;
    ProcessTestInitializeNativeParameters(
        &Parameters,
        "processChild.mtexe",
        ProcessTestCommandLine
    );

    MT_PROCESS_INFORMATION Information = {
        .ProcessHandle = MT_INVALID_HANDLE,
        .ThreadHandle = MT_INVALID_HANDLE
    };
    const void* InvalidUserPointer =
        (const void*)(MT_HIGHEST_USER_ADDRESS + 1ULL);

    MTSTATUS Status = MtCreateProcess(
        (const MT_CREATE_PROCESS_PARAMETERS*)InvalidUserPointer,
        &Information
    );
    if (Status != MT_ACCESS_VIOLATION) {
        return MT_PROCESS_TEST_NATIVE_PARAMETER_PTR;
    }

    Status = MtCreateProcess(
        &Parameters,
        (PMT_PROCESS_INFORMATION)(uintptr_t)InvalidUserPointer
    );
    if (Status != MT_ACCESS_VIOLATION) {
        return MT_PROCESS_TEST_NATIVE_OUTPUT_PTR;
    }

    MT_CREATE_PROCESS_PARAMETERS InvalidParameters = Parameters;
    InvalidParameters.ImagePath = (const char*)InvalidUserPointer;
    Status = MtCreateProcess(&InvalidParameters, &Information);
    if (Status != MT_ACCESS_VIOLATION) {
        return MT_PROCESS_TEST_NATIVE_IMAGE_PTR;
    }

    Status = MtCreateProcess(
        (const MT_CREATE_PROCESS_PARAMETERS*)((const char*)&Parameters + 1),
        &Information
    );
    if (Status != MT_DATATYPE_MISALIGNMENT) {
        return MT_PROCESS_TEST_NATIVE_ALIGNMENT;
    }

    void* ReadOnlyInformation = VirtualAlloc(
        NULL,
        sizeof(MT_PROCESS_INFORMATION),
        PAGE_READONLY
    );
    if (!ReadOnlyInformation) {
        return MT_PROCESS_TEST_READONLY_OUTPUT;
    }

    Status = MtCreateProcess(&Parameters, ReadOnlyInformation);
    bool ReadOnlyFreed = VirtualFree(ReadOnlyInformation, 0, MEM_RELEASE);
    if (Status != MT_ACCESS_VIOLATION || !ReadOnlyFreed) {
        return MT_PROCESS_TEST_READONLY_OUTPUT;
    }

    ProcessTestInitializeNativeParameters(
        &Parameters,
        "invalidProcess.mtexe",
        "invalidProcess.mtexe"
    );
    Information = (MT_PROCESS_INFORMATION){
        .ProcessHandle = MT_INVALID_HANDLE,
        .ThreadHandle = MT_INVALID_HANDLE
    };
    Status = MtCreateProcess(&Parameters, &Information);
    if (Status != MT_INVALID_IMAGE_FORMAT) {
        return MT_PROCESS_TEST_INVALID_IMAGE;
    }
    if (Information.ProcessHandle != MT_INVALID_HANDLE ||
        Information.ThreadHandle != MT_INVALID_HANDLE ||
        Information.ProcessId != 0 || Information.ThreadId != 0) {
        return MT_PROCESS_TEST_INVALID_IMAGE_OUTPUT;
    }

    return MT_SUCCESS;
}

NORETURN
static void
ProcessTestExit(
    IN MTSTATUS Status
)

/*++

    Routine description:

        Terminates the process-test parent with the supplied result.

    Arguments:

        [IN] Status - Status returned to the kernel process-test controller.

    Return Values:

        None.

--*/

{
    TerminateProcess(MtCurrentProcess(), (uint32_t)Status);
    for (;;) {
    }
}

static
MTSTATUS
ProcessTestCreateAndWait(
    void
)

/*++

    Routine description:

        Creates one child and validates its handles, identifiers, waits, and exit status.

    Arguments:

        None.

    Return Values:

        MT_SUCCESS when the child completes correctly, or a process-test failure status.

--*/

{
    PROCESS_INFORMATION Information = { 0 };
    if (!CreateProcess(
            "processChild.mtexe",
            ProcessTestCommandLine,
            "process-test-dir",
            ProcessTestEnvironment,
            sizeof(ProcessTestEnvironment),
            &Information)) {
        return MT_PROCESS_TEST_CREATE;
    }

    if (Information.ProcessHandle == MT_INVALID_HANDLE ||
        Information.ThreadHandle == MT_INVALID_HANDLE ||
        Information.ProcessId == 0 || Information.ThreadId == 0) {
        return MT_PROCESS_TEST_OUTPUT;
    }

    if (WaitForSingleObject(Information.ThreadHandle, 30000) != WAIT_OBJECT_0) {
        return MT_PROCESS_TEST_THREAD_WAIT;
    }
    if (WaitForSingleObject(Information.ProcessHandle, 30000) != WAIT_OBJECT_0) {
        return MT_PROCESS_TEST_PROCESS_WAIT;
    }

    uint32_t ExitCode = 0;
    if (!GetExitCodeThread(Information.ThreadHandle, &ExitCode) ||
        (MTSTATUS)ExitCode != MT_SUCCESS) {
        return MT_PROCESS_TEST_THREAD_QUERY;
    }
    if (!GetExitCodeProcess(Information.ProcessHandle, &ExitCode) ||
        (MTSTATUS)ExitCode != MT_SUCCESS) {
        return MT_PROCESS_TEST_PROCESS_QUERY;
    }

    if (!CloseHandle(Information.ThreadHandle)) {
        return MT_PROCESS_TEST_THREAD_CLOSE;
    }
    if (!CloseHandle(Information.ProcessHandle)) {
        return MT_PROCESS_TEST_PROCESS_CLOSE;
    }
    return MT_SUCCESS;
}

static
uint32_t
ProcessTestWorker(
    IN void* Parameter
)

/*++

    Routine description:

        Repeatedly creates and waits for children from one parent worker thread.

    Arguments:

        [IN] Parameter - Unused worker context.

    Return Values:

        MT_SUCCESS when every child passes, or the first process-test failure status.

--*/

{
    (void)Parameter;
    for (uint32_t Index = 0; Index < PROCESS_TEST_CHILDREN_PER_RUN; Index++) {
        MTSTATUS Status = ProcessTestCreateAndWait();
        if (MT_FAILURE(Status)) return (uint32_t)Status;
    }
    return (uint32_t)MT_SUCCESS;
}

static
void
ProcessTestFailurePaths(
    void
)

/*++

    Routine description:

        Verifies missing-image and malformed-environment process creation rollback.

    Arguments:

        None.

    Return Values:

        None. A failed check terminates the process with its test status.

--*/

{
    PROCESS_INFORMATION Information = { 0 };
    if (CreateProcess(
            "missingProcess.mtexe",
            NULL,
            NULL,
            NULL,
            0,
            &Information)) {
        ProcessTestExit(MT_PROCESS_TEST_MISSING_IMAGE);
    }
    if (Information.ProcessHandle != MT_INVALID_HANDLE ||
        Information.ThreadHandle != MT_INVALID_HANDLE ||
        Information.ProcessId != 0 || Information.ThreadId != 0) {
        ProcessTestExit(MT_PROCESS_TEST_MISSING_OUTPUT);
    }

    static const char BadEnvironment[] = { 'B', 'A', 'D', '=', '1', '\0', 'X' };
    Information = (PROCESS_INFORMATION){ 0 };
    if (CreateProcess(
            "processChild.mtexe",
            ProcessTestCommandLine,
            "process-test-dir",
            BadEnvironment,
            sizeof(BadEnvironment),
            &Information)) {
        ProcessTestExit(MT_PROCESS_TEST_BAD_ENVIRONMENT);
    }
    if (Information.ProcessHandle != MT_INVALID_HANDLE ||
        Information.ThreadHandle != MT_INVALID_HANDLE ||
        Information.ProcessId != 0 || Information.ThreadId != 0) {
        ProcessTestExit(MT_PROCESS_TEST_BAD_ENVIRONMENT_OUTPUT);
    }
}

int
main(
    void
)

/*++

    Routine description:

        Runs public and native process-creation, rollback, lifetime, and
        concurrent child-creation tests.

    Arguments:

        None.

    Return Values:

        This routine terminates the process with its test result.

--*/

{
    MTSTATUS Status = ProcessTestThreadPriorityApi();
    if (MT_FAILURE(Status)) ProcessTestExit(Status);

    Status = ProcessTestCreateAndWait();
    if (MT_FAILURE(Status)) ProcessTestExit(Status);

    ProcessTestFailurePaths();

    Status = ProcessTestNativeFailurePaths();
    if (MT_FAILURE(Status)) ProcessTestExit(Status);

    HANDLE Workers[PROCESS_TEST_WORKER_COUNT] = { 0 };
    for (uint32_t Index = 0; Index < PROCESS_TEST_WORKER_COUNT; Index++) {
        Workers[Index] = CreateThread(ProcessTestWorker, NULL);
        if (Workers[Index] == MT_INVALID_HANDLE) {
            ProcessTestExit(MT_PROCESS_TEST_WORKER_CREATE);
        }
    }

    for (uint32_t Index = 0; Index < PROCESS_TEST_WORKER_COUNT; Index++) {
        if (WaitForSingleObject(Workers[Index], 120000) != WAIT_OBJECT_0) {
            ProcessTestExit(MT_PROCESS_TEST_WORKER_WAIT);
        }

        uint32_t ExitCode = 0;
        if (!GetExitCodeThread(Workers[Index], &ExitCode)) {
            ProcessTestExit(MT_PROCESS_TEST_WORKER_QUERY);
        }
        if (!CloseHandle(Workers[Index])) {
            ProcessTestExit(MT_PROCESS_TEST_WORKER_CLOSE);
        }
        if ((MTSTATUS)ExitCode != MT_SUCCESS) {
            ProcessTestExit(MT_PROCESS_TEST_CONCURRENT_CREATE);
        }
    }

    PROCESS_INFORMATION OrphanInformation = { 0 };
    if (!CreateProcess(
            "processChild.mtexe",
            "processChild.mtexe --orphan",
            NULL,
            NULL,
            0,
            &OrphanInformation)) {
        ProcessTestExit(MT_PROCESS_TEST_ORPHAN_CREATE);
    }

    // Leave both handles in this process. Process teardown must close them
    // without terminating the independently referenced child process.
    ProcessTestExit((MTSTATUS)OrphanInformation.ProcessId);
}
