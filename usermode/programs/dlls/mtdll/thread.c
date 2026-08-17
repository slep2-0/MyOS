/*++

Module Name:

    thread.c

Purpose:

    This translation unit contains the standard library functions involving thread operations.

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "includes/mtdll.h"
#include "includes/errorhandlingapi.h"
#include "includes/processthreadsapi.h"

MTDLL_API
bool
TerminateThread(
    IN HANDLE ThreadHandle,
    IN uint32_t ExitStatus
)

/*++

    Routine description:

        Requests termination of a thread through the native API.

    Arguments:

        [IN] ThreadHandle - The handle of the thread to terminate.
        [IN] ExitStatus - The status assigned to the thread on termination.

    Return Values:

        true when termination is requested successfully, or false when the
        native operation fails.

    Notes:

        This routine requests termination; it does not wait for the thread to
        finish terminating. Forced termination bypasses the normal MTDLL
        thread-return path, so user-mode TLS teardown is deferred until process
        exit when the target does not return through that path.

--*/

{
    // Assume ExitStatus is MTSTATUS for now, we need termination ports for custom statuses.
    MTSTATUS Status = MtTerminateThread(ThreadHandle, ExitStatus);

    SetLastStatus(Status);
    SetLastError(MtStatusToLastError(Status));

    return MT_SUCCEEDED(Status);
}

MTDLL_API
HANDLE
CreateThread(
    IN THREAD_START_ROUTINE StartRoutine,
    IN void* ThreadParameter
)

/*++

    Routine description:

        Creates a thread in the current process.

    Arguments:

        [IN] StartRoutine - The user-mode routine executed by the new thread.
        [IN] ThreadParameter - The value passed to StartRoutine.

    Return Values:

        A valid thread handle on success, or MT_INVALID_HANDLE on failure.

--*/

{
    HANDLE ThreadHandle = MT_INVALID_HANDLE;
    MTSTATUS Status = MtCreateThread(MtCurrentProcess(), StartRoutine, ThreadParameter, &ThreadHandle);
    SetLastError(MtStatusToLastError(Status));
    SetLastStatus(Status);
    return ThreadHandle;
}

MTDLL_API
HANDLE
CreateRemoteThread(
    IN HANDLE ProcessHandle,
    IN THREAD_START_ROUTINE StartRoutine,
    IN void* ThreadParameter
)

/*++

    Routine description:

        Creates a thread in a target process.

    Arguments:

        [IN] ProcessHandle - The handle of the target process.
        [IN] StartRoutine - The user-mode routine executed by the new thread.
        [IN] ThreadParameter - The value passed to StartRoutine.

    Return Values:

        A valid thread handle on success, or MT_INVALID_HANDLE on failure.

--*/

{
    HANDLE ThreadHandle = MT_INVALID_HANDLE;
    MTSTATUS Status = MtCreateThread(ProcessHandle, StartRoutine, ThreadParameter, &ThreadHandle);
    SetLastError(MtStatusToLastError(Status));
    SetLastStatus(Status);
    return ThreadHandle;
}

MTDLL_API
bool
GetExitCodeThread(
    IN HANDLE ThreadHandle,
    OUT uint32_t* ExitCode
)

/*++

    Routine description:

        Retrieves the exit status of a thread.

    Arguments:

        [IN] ThreadHandle - The handle of the thread to query.
        [OUT] ExitCode - Receives the thread exit code.

    Return Values:

        true when the exit code is returned, or false when the query fails.

--*/

{
    THREAD_BASIC_INFORMATION BasicInfo;
    MTSTATUS Status = MtQueryInformationThread(ThreadHandle, ThreadBasicInformation, &BasicInfo, sizeof(THREAD_BASIC_INFORMATION), NULL);
    SetLastStatus(Status);
    SetLastError(MtStatusToLastError(Status));

    bool Success = MT_SUCCEEDED(Status);

    if (Success) {
        *ExitCode = BasicInfo.ExitStatus;
    }

    return Success;
}

MTDLL_API uint32_t
SuspendThread(
    IN HANDLE ThreadHandle
)

/*++

    Routine description:

        Increments the suspension count of a thread.

    Arguments:

        [IN] ThreadHandle - The handle of the thread to suspend.

    Return Values:

        The previous suspension count, or UINT32_MAX when the operation
        fails.

    Notes:

        A successful call may leave the target running until its suspension
        request is observed at a safe scheduling/APC boundary.

--*/

{
    uint32_t PreviousCount;
    MTSTATUS Status = MtSuspendThread(ThreadHandle, &PreviousCount);
    SetLastStatus(Status);
    SetLastError(MtStatusToLastError(Status));

    if (MT_FAILURE(Status)) {
        return UINT32_MAX;
    }
    else {
        return PreviousCount;
    }
}

MTDLL_API uint32_t
ResumeThread(
    IN HANDLE ThreadHandle
)

/*++

    Routine description:

        Decrements the suspension count of a thread.

    Arguments:

        [IN] ThreadHandle - The handle of the thread to resume.

    Return Values:

        The previous suspension count, or UINT32_MAX when the operation
        fails.

    Notes:

        The target becomes eligible to run when its suspension count reaches
        zero and no other blocking state prevents execution.

--*/

{
    uint32_t PreviousCount;
    MTSTATUS Status = MtResumeThread(ThreadHandle, &PreviousCount);
    SetLastStatus(Status);
    SetLastError(MtStatusToLastError(Status));

    if (MT_FAILURE(Status)) {
        return UINT32_MAX;
    }
    else {
        return PreviousCount;
    }
}
