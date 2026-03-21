#pragma once

/*++

Module Name:

	errorhandlingapi.h

Purpose:

	This header contains the prototypes, structures, enumerators, and functions required for error handling in user mode.

Author:

	slep (Matanel) 2025.

Revision History:

--*/

#include <stdint.h>
#include "annotations.h"
#include "mtstatus.h"
typedef int32_t ERROR_CODE;

/* Success */
#define ERROR_SUCCESS                    ((ERROR_CODE)0x00000000L)

/* General */
#define ERROR_INVALID_FUNCTION           ((ERROR_CODE)1L)
#define ERROR_FILE_NOT_FOUND             ((ERROR_CODE)2L)
#define ERROR_PATH_NOT_FOUND             ((ERROR_CODE)3L)
#define ERROR_TOO_MANY_OPEN_FILES        ((ERROR_CODE)4L)
#define ERROR_ACCESS_DENIED              ((ERROR_CODE)5L)
#define ERROR_INVALID_HANDLE             ((ERROR_CODE)6L)
#define ERROR_NOT_ENOUGH_MEMORY          ((ERROR_CODE)8L)
#define ERROR_INVALID_DATA               ((ERROR_CODE)13L)
#define ERROR_OUTOFMEMORY                ((ERROR_CODE)14L)
#define ERROR_INVALID_DRIVE              ((ERROR_CODE)15L)
#define ERROR_NO_MORE_FILES              ((ERROR_CODE)18L)
#define ERROR_WRITE_PROTECT              ((ERROR_CODE)19L)
#define ERROR_BAD_UNIT                   ((ERROR_CODE)20L)
#define ERROR_NOT_READY                  ((ERROR_CODE)21L)
#define ERROR_BAD_COMMAND                ((ERROR_CODE)22L)
#define ERROR_CRC                        ((ERROR_CODE)23L)
#define ERROR_BAD_LENGTH                 ((ERROR_CODE)24L)
#define ERROR_SEEK                       ((ERROR_CODE)25L)
#define ERROR_NOT_DOS_DISK               ((ERROR_CODE)26L)
#define ERROR_SECTOR_NOT_FOUND           ((ERROR_CODE)27L)
#define ERROR_OUT_OF_PAPER               ((ERROR_CODE)28L)
#define ERROR_WRITE_FAULT                ((ERROR_CODE)29L)
#define ERROR_READ_FAULT                 ((ERROR_CODE)30L)
#define ERROR_GEN_FAILURE                ((ERROR_CODE)31L)
#define ERROR_SHARING_VIOLATION          ((ERROR_CODE)32L)
#define ERROR_LOCK_VIOLATION             ((ERROR_CODE)33L)
#define ERROR_WRONG_DISK                 ((ERROR_CODE)34L)
#define ERROR_HANDLE_EOF                 ((ERROR_CODE)38L)
#define ERROR_HANDLE_DISK_FULL           ((ERROR_CODE)39L)
#define ERROR_NOT_SUPPORTED              ((ERROR_CODE)50L)
#define ERROR_FILE_EXISTS                ((ERROR_CODE)80L)
#define ERROR_INVALID_PARAMETER          ((ERROR_CODE)87L)
#define ERROR_BROKEN_PIPE                ((ERROR_CODE)109L)
#define ERROR_BUFFER_OVERFLOW            ((ERROR_CODE)111L)
#define ERROR_DISK_FULL                  ((ERROR_CODE)112L)
#define ERROR_INVALID_NAME               ((ERROR_CODE)123L)
#define ERROR_MOD_NOT_FOUND              ((ERROR_CODE)126L)
#define ERROR_PROC_NOT_FOUND             ((ERROR_CODE)127L)
#define ERROR_ALREADY_EXISTS             ((ERROR_CODE)183L)
#define ERROR_BAD_PATHNAME               ((ERROR_CODE)161L)
#define ERROR_DIR_NOT_EMPTY              ((ERROR_CODE)145L)
#define ERROR_FILENAME_EXCED_RANGE       ((ERROR_CODE)206L)
#define ERROR_DIRECTORY                  ((ERROR_CODE)267L)

/* I/O / async */
#define ERROR_IO_INCOMPLETE              ((ERROR_CODE)996L)
#define ERROR_IO_PENDING                 ((ERROR_CODE)997L)
#define ERROR_NOACCESS                   ((ERROR_CODE)998L)
#define ERROR_OPERATION_ABORTED          ((ERROR_CODE)995L)
#define ERROR_IO_DEVICE                  ((ERROR_CODE)1117L)

/* User-facing system */
#define ERROR_NOT_ENOUGH_QUOTA           ((ERROR_CODE)1816L)
#define ERROR_TIMEOUT                    ((ERROR_CODE)1460L)
#define ERROR_NOT_FOUND                  ((ERROR_CODE)1168L)
#define ERROR_NOT_IMPLEMENTED            ((ERROR_CODE)120L)


// Functions definitions themselves.

ERROR_CODE GetLastError(
	void
);

void SetLastError(
	ERROR_CODE dwErrorCode
);

FORCEINLINE
ERROR_CODE
MtStatusToLastError(MTSTATUS Status)
{
    switch (Status)
    {
    case MT_SUCCESS:
        return ERROR_SUCCESS;

    case MT_NOT_IMPLEMENTED:
        return ERROR_NOT_IMPLEMENTED;

    case MT_INVALID_PARAM:
        return ERROR_INVALID_PARAMETER;

    case MT_INVALID_HANDLE:
        return ERROR_INVALID_HANDLE;

    case MT_ACCESS_DENIED:
        return ERROR_ACCESS_DENIED;

    case MT_TIMEOUT:
        return ERROR_TIMEOUT;

    case MT_ALREADY_EXISTS:
        return ERROR_ALREADY_EXISTS;

    case MT_NOT_FOUND:
        return ERROR_NOT_FOUND;

    case MT_NO_MEMORY:
    case MT_MEMORY_LIMIT:
    case MT_HEAP_CORRUPTION:
        return ERROR_OUTOFMEMORY;

    case MT_INVALID_ADDRESS:
        return ERROR_INVALID_PARAMETER;

    case MT_IO_ERROR:
    case MT_DEVICE_ERROR:
    case MT_AHCI_GENERAL_FAILURE:
        return ERROR_GEN_FAILURE;

    case MT_VFS_NO_SPACE:
    case MT_FAT32_CLUSTERS_FULL:
        return ERROR_DISK_FULL;

    case MT_FAT32_FILE_NOT_FOUND:
    case MT_FAT32_DIRECTORY_NOT_FOUND:
        return ERROR_FILE_NOT_FOUND;

    case MT_FAT32_PATH_TOO_LONG:
    case MT_FAT32_FILENAME_TOO_LONG:
        return ERROR_FILENAME_EXCED_RANGE;

    case MT_FAT32_INVALID_FILENAME:
        return ERROR_INVALID_NAME;

    default:
        return ERROR_GEN_FAILURE;
    }
}

