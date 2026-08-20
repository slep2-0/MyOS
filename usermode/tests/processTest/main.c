#include <MatanelOS.h>
#include <mtstatus.h>

#include "process_test.h"

#define PROCESS_TEST_WORKER_COUNT     4
#define PROCESS_TEST_CHILDREN_PER_RUN 4

static const char ProcessTestCommandLine[] =
    "processChild.mtexe alpha \"two words\" \"\"";
static const char ProcessTestEnvironment[] =
    "PROCESS_TEST=child\0"
    "SECOND=two\0";

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

        Runs the public process-creation API and concurrent child-creation tests.

    Arguments:

        None.

    Return Values:

        This routine terminates the process with its test result.

--*/

{
    MTSTATUS Status = ProcessTestCreateAndWait();
    if (MT_FAILURE(Status)) ProcessTestExit(Status);

    ProcessTestFailurePaths();

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

    ProcessTestExit(MT_SUCCESS);
}
