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

bool
TerminateThread(
    IN HANDLE ThreadHandle,
    IN uint32_t ExitStatus
)

{
    // Assume ExitStatus is MTSTATUS for now, we need termination ports for custom statuses.
    MTSTATUS Status = MtTerminateThread(ThreadHandle, ExitStatus);

    SetLastStatus(Status);
    SetLastError(MtStatusToLastError(Status));

    return MT_SUCCEEDED(Status);
}

HANDLE
CreateThread(
    IN THREAD_START_ROUTINE StartRoutine,
    IN void* ThreadParameter
)

{
    HANDLE ThreadHandle = MT_INVALID_HANDLE;
    MTSTATUS Status = MtCreateThread(MtCurrentProcess(), StartRoutine, ThreadParameter, &ThreadHandle);
    SetLastError(MtStatusToLastError(Status));
    SetLastStatus(Status);
    return ThreadHandle;
}

HANDLE
CreateRemoteThread(
    IN HANDLE ProcessHandle,
    IN THREAD_START_ROUTINE StartRoutine,
    IN void* ThreadParameter
)

{
    HANDLE ThreadHandle = MT_INVALID_HANDLE;
    MTSTATUS Status = MtCreateThread(ProcessHandle, StartRoutine, ThreadParameter, &ThreadHandle);
    SetLastError(MtStatusToLastError(Status));
    SetLastStatus(Status);
    return ThreadHandle;
}

bool
GetExitCodeThread(
    IN HANDLE ThreadHandle,
    OUT uint32_t* ExitCode
)

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