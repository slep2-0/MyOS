/*++

Module Name:

    process.c

Purpose:

    This translation unit contains the standard library functions involving process creation, termination, and acquirement.

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "includes/mtdll.h"
#include "includes/exports.h"
#include "includes/errorhandlingapi.h"

HANDLE
OpenProcess(
    IN  ACCESS_MASK DesiredAccess,
    IN  uint32_t ProcessId
)

{
    // Call kernel.
    HANDLE OutHandle = MT_INVALID_HANDLE;
    MTSTATUS Status = MtOpenProcess(ProcessId, &OutHandle, DesiredAccess);

    SetLastError(MtStatusToLastError(Status));
    if (MT_FAILURE(Status)) return MT_INVALID_HANDLE;

    // Return handle.
    return OutHandle;
}

bool
TerminateProcess(
    IN  HANDLE ProcessHandle,
    IN  uint32_t ExitCode
)

{
    // Since we dont have termination ports for a process (so we can feed the exit code in), we assume exit code is MTSTATUS
    MTSTATUS Status = MtTerminateProcess(ProcessHandle, ExitCode);
    SetLastError(MtStatusToLastError(Status));

    return MT_SUCCEEDED(Status);
}