/*++

Module Name:

    generic.c

Purpose:

    This translation unit contains the standard library functions involving generic operations.
    
Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "includes/mtdll.h"
#include "includes/errorhandlingapi.h"

bool
CloseHandle(
    IN HANDLE hObject
)

{
    // Call kernel, retrieve status.
    MTSTATUS Status = MtClose(hObject);
    SetLastError(MtStatusToLastError(Status));

    return MT_SUCCEEDED(Status);
}

