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
    IN const char* FileName,
    IN ACCESS_MASK DesiredAccess,
    IN FILE_CREATION_DISPOSITION CreationDisposition
)

/*++

    Routine description:

        Creates a file handle by forwarding the requested access to the native
        file service.

    Arguments:

        [IN] FileName - The path of the file to open.
        [IN] DesiredAccess - The access mask requested for the file handle.
        [IN] CreationDisposition - The file creation flags.

    Return Values:

        A valid file handle on success, or MT_INVALID_HANDLE on failure.

    Notes:

        The native status and translated last-error value are updated before
        the routine returns.

--*/

{
    // Set default
    HANDLE OutHandle = MT_INVALID_HANDLE;

    // Call kernel
    MTSTATUS Status = MtCreateFile(FileName, DesiredAccess, CreationDisposition, &OutHandle);

    SetLastStatus(Status);
    SetLastError(MtStatusToLastError(Status));
    if (MT_FAILURE(Status)) return MT_INVALID_HANDLE;

    // Return handle if it got updated.
    return OutHandle;
}

MTDLL_API
bool
WriteFile(
    IN HANDLE FileHandle,
    IN uint32_t FileOffset,
    IN void* Buffer,
    IN size_t BufferSize,
    _Out_Opt size_t* BytesWritten
)

/*++

    Routine description:

        Writes a byte range to an opened file handle.

    Arguments:

        [IN] FileHandle - The handle of the file to modify.
        [IN] FileOffset - The byte offset at which writing begins.
        [IN] Buffer - The source buffer containing the bytes to write.
        [IN] BufferSize - The number of bytes to write.
        [OUT OPTIONAL] BytesWritten - Receives the number of bytes written.

    Return Values:

        true when the write succeeds, or false when the native operation
        fails.

    Notes:

        The native status and translated last-error value are updated before
        the routine returns.

--*/

{
    // Call kernel, retrieve status.
    MTSTATUS Status = MtWriteFile(FileHandle, FileOffset, Buffer, BufferSize, BytesWritten);
    
    SetLastStatus(Status);
    SetLastError(MtStatusToLastError(Status));
    return MT_SUCCEEDED(Status);
}


MTDLL_API
bool
ReadFile(
    IN HANDLE FileHandle,
    IN uint32_t FileOffset,
    OUT void* Buffer,
    IN size_t BufferSize,
    _Out_Opt size_t* BytesRead
)

/*++

    Routine description:

        Reads a byte range from an opened file handle.

    Arguments:

        [IN] FileHandle - The handle of the file to read.
        [IN] FileOffset - The byte offset at which reading begins.
        [OUT] Buffer - The destination buffer for the bytes that are read.
        [IN] BufferSize - The maximum number of bytes to read.
        [OUT OPTIONAL] BytesRead - Receives the number of bytes read.

    Return Values:

        true when the read succeeds, or false when the native operation
        fails.

    Notes:

        The native status and translated last-error value are updated before
        the routine returns.

--*/

{
    // Call kernel, retrieve status.
    MTSTATUS Status = MtReadFile(FileHandle, FileOffset, Buffer, BufferSize, BytesRead);

    SetLastStatus(Status);
    SetLastError(MtStatusToLastError(Status));
    return MT_SUCCEEDED(Status);
}
