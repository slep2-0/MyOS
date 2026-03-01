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

HANDLE
OpenProcess(
    IN  ACCESS_MASK DesiredAccess,
    IN  uint32_t ProcessId
)

{
    // Call kernel.
    HANDLE OutHandle = MT_INVALID_HANDLE;
    MTSTATUS Status = MtOpenProcess(ProcessId, &OutHandle, DesiredAccess);
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
    if (MT_FAILURE(MtTerminateProcess(ProcessHandle, ExitCode))) {
        return false;
    }

    return true;
}

bool
TerminateThread(
    IN HANDLE ThreadHandle,
    IN uint32_t ExitCode
)

{
    // This should be ran at every thread final return (including main thread, it shouldn't call ExitProcess)
    MTSTATUS Status = MtTerminateThread(ThreadHandle, ExitCode);
    return MT_SUCCEEDED(Status);
}

