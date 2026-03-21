/*++

Module Name:

    file.c

Purpose:

    This translation unit contains the standard library functions involving file operations.

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "includes/mtdll.h"
#include "includes/exports.h"
#include "includes/errorhandlingapi.h"

HANDLE
CreateFile(
    IN  const char* FileName,
    IN  ACCESS_MASK DesiredAccess
)

{
    // Set default
    HANDLE OutHandle = MT_INVALID_HANDLE;

    // Call kernel
    MTSTATUS Status = MtCreateFile(FileName, DesiredAccess, &OutHandle);

    SetLastStatus(Status);
    SetLastError(MtStatusToLastError(Status));
    if (MT_FAILURE(Status)) return MT_INVALID_HANDLE;

    // Return handle if it got updated.
    return OutHandle;
}

bool
WriteFile(
    IN HANDLE FileHandle,
    IN uint32_t FileOffset,
    IN void* Buffer,
    IN size_t BufferSize,
    _Out_Opt size_t* BytesWritten
)

{
    // Call kernel, retrieve status.
    MTSTATUS Status = MtWriteFile(FileHandle, FileOffset, Buffer, BufferSize, BytesWritten);
    
    SetLastStatus(Status);
    SetLastError(MtStatusToLastError(Status));
    return MT_SUCCEEDED(Status);
}


bool
ReadFile(
    IN HANDLE FileHandle,
    IN uint32_t FileOffset,
    OUT void* Buffer,
    IN size_t BufferSize,
    _Out_Opt size_t* BytesRead
)

{
    // Call kernel, retrieve status.
    MTSTATUS Status = MtReadFile(FileHandle, FileOffset, Buffer, BufferSize, BytesRead);

    SetLastStatus(Status);
    SetLastError(MtStatusToLastError(Status));
    return MT_SUCCEEDED(Status);
}