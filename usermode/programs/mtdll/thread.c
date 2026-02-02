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

bool
TerminateThread(
    IN HANDLE ThreadHandle,
    IN uint32_t ExitStatus
)

{
    // Assume ExitStatus is MTSTATUS for now, we need termination ports for custom statuses.
    MTSTATUS Status = MtTerminateThread(ThreadHandle, ExitStatus);

    return MT_SUCCEEDED(Status);
}

