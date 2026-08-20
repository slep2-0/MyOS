/*++

Module Name:

    handle.c

Purpose:

    This translation unit contains the standard library functions involving handle operations.
    
Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "includes/mtdll.h"
#include "includes/errorhandlingapi.h"

MTDLL_API
bool
CloseHandle(
    IN HANDLE hObject
)

/*++

    Routine description:

        Closes a user-mode handle and releases the caller's handle-table
        reference to its object.

    Arguments:

        [IN] hObject - The handle to close.

    Return Values:

        true when the handle is closed, or false when the handle is invalid or
        the native close operation fails.

    Notes:

        The native status and translated last-error value are updated before
        the routine returns.

--*/

{
    // Call kernel, retrieve status.
    MTSTATUS Status = MtClose(hObject);
    SetLastStatus(Status);
    SetLastError(MtStatusToLastError(Status));

    return MT_SUCCEEDED(Status);
}
