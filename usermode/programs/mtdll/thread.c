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