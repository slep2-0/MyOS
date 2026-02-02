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

bool
CloseHandle(
    IN HANDLE hObject
)

{
    // Call kernel, retrieve status.
    MTSTATUS Status = MtClose(hObject);

    return MT_SUCCEEDED(Status);
}

