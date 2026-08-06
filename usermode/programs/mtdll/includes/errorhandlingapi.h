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

#include "mtdll.h"

void SetLastStatus(
    MTSTATUS dwStatusCode
);

MTSTATUS GetLastStatus(
    void
);

FORCEINLINE 
ERROR_CODE 
MtStatusToLastError(MTSTATUS Status)
{
    // Fast path for success
    if (Status == MT_SUCCESS) {
        return ERROR_SUCCESS;
    }

    switch (Status)
    {
        // -------------------------
        // General & Base
        // -------------------------
    case MT_NOT_IMPLEMENTED:
        return ERROR_NOT_IMPLEMENTED;
    case MT_INVALID_PARAM:
    case MT_INVALID_CHECK:
    case MT_TYPE_MISMATCH:
        return ERROR_INVALID_PARAMETER;
    case MT_INVALID_STATE:
        return ERROR_INVALID_DATA;
    case MT_ACCESS_DENIED:
        return ERROR_ACCESS_DENIED;
    case MT_TIMEOUT:
        return ERROR_TIMEOUT;
    case MT_UNSUPPORTED_OP:
        return ERROR_NOT_SUPPORTED;
    case MT_ALREADY_EXISTS:
        return ERROR_ALREADY_EXISTS;
    case MT_NOT_FOUND:
        return ERROR_NOT_FOUND;
    case MT_NO_RESOURCES:
    case MT_NO_MEMORY:
        return ERROR_OUTOFMEMORY;
    case MT_INVALID_HANDLE:
        return ERROR_INVALID_HANDLE;

        // -------------------------
        // Memory 
        // -------------------------
    case MT_MEMORY_LIMIT:
        return ERROR_NOT_ENOUGH_QUOTA;
    case MT_INVALID_ADDRESS:
    case MT_CONFLICTING_ADDRESSES:
    case MT_PAGE_FAULT_ERROR:
        return ERROR_NOACCESS;
    case MT_INVALID_IMAGE_FORMAT:
        return ERROR_BAD_UNIT;
    case MT_DATATYPE_MISALIGNMENT:
        return ERROR_DATATYPE_MISMATCH;

        // -------------------------
        // VFS & Filesystem Base
        // -------------------------
    case MT_IO_ERROR:
        return ERROR_IO_DEVICE;
    case MT_VFS_CORRUPTED:
        return ERROR_CRC;
    case MT_VFS_READ_ONLY:
        return ERROR_WRITE_PROTECT;
    case MT_VFS_NO_SPACE:
        return ERROR_DISK_FULL;
    case MT_VFS_PERMISSION_DENIED:
        return ERROR_ACCESS_DENIED;

        // -------------------------
        // FAT32 Specific
        // -------------------------
    case MT_FAT32_CLUSTERS_FULL:
    case MT_FAT32_DIR_FULL:
        return ERROR_DISK_FULL;
    case MT_FAT32_FILE_NOT_FOUND:
        return ERROR_FILE_NOT_FOUND;
    case MT_FAT32_DIRECTORY_NOT_FOUND:
    case MT_FAT32_PARENT_PATH_NOT_FOUND:
        return ERROR_PATH_NOT_FOUND;
    case MT_FAT32_PATH_TOO_LONG:
    case MT_FAT32_FILENAME_TOO_LONG:
        return ERROR_FILENAME_EXCED_RANGE;
    case MT_FAT32_INVALID_FILENAME:
        return ERROR_INVALID_NAME;
    case MT_FAT32_EOF:
        return ERROR_HANDLE_EOF;
    case MT_FAT32_DIRECTORY_ALREADY_EXISTS:
        return ERROR_ALREADY_EXISTS;
    case MT_FAT32_PARENT_PATH_NOT_DIR:
        return ERROR_DIRECTORY;
    case MT_FAT32_INVALID_WRITE_MODE:
        return ERROR_INVALID_PARAMETER;
    case MT_FAT32_INVALID_CLUSTER:
    case MT_FAT32_CLUSTER_NOT_FOUND:
        return ERROR_SECTOR_NOT_FOUND;

        // -------------------------
        // Driver / Device / AHCI
        // -------------------------
    case MT_DEVICE_NOT_READY:
        return ERROR_NOT_READY;
    case MT_DEVICE_ERROR:
    case MT_AHCI_PORT_FAILURE:
    case MT_AHCI_READ_FAILURE:
    case MT_AHCI_WRITE_FAILURE:
        return ERROR_IO_DEVICE;
    case MT_DEVICE_TIMEOUT:
    case MT_AHCI_TIMEOUT:
        return ERROR_TIMEOUT;
    case MT_DEVICE_UNSUPPORTED:
        return ERROR_NOT_SUPPORTED;

        // -------------------------
        // Synchronization & Mutex
        // -------------------------
    case MT_MUTEX_ALREADY_OWNED:
    case MT_EVENT_ALREADY_SIGNALED:
        return ERROR_ALREADY_EXISTS;
    case MT_MUTEX_NOT_OWNED:
    case MT_INVALID_LOCK:
        return ERROR_INVALID_PARAMETER; // Or ERROR_ACCESS_DENIED depending on semantics
    case MT_SEMAPHORE_LIMIT_EXCEEDED:
        return ERROR_TOO_MANY_POSTS;

        // -------------------------
        // Process / Thread / User Mode
        // -------------------------
    case MT_INVALID_SYSTEM_SERVICE:
        return ERROR_INVALID_FUNCTION;
    case MT_PROCESS_IS_TERMINATING:
    case MT_CANT_TERMINATE_SELF:
        return ERROR_ACCESS_DENIED;

        // -------------------------
        // Exceptions
        // -------------------------
    case MT_ACCESS_VIOLATION:
    case MT_GUARD_PAGE_VIOLATION:
        return ERROR_NOACCESS;

        // -------------------------
        // General Failures
        // -------------------------
    case MT_GENERAL_FAILURE:
    case MT_VFS_GENERAL_FAILURE:
    case MT_VFS_INITIALIZATION_FAILURE:
    case MT_FAT32_CLUSTER_GENERAL_FAILURE:
    case MT_AHCI_GENERAL_FAILURE:
    case MT_AHCI_INIT_FAILED:
    case MT_SCHEDULER_ERROR:
    case MT_THREAD_CREATION_FAILURE:
    default:
        return ERROR_GEN_FAILURE;
    }
}

// soon to be raise exception
