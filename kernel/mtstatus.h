/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      MTStatus definitions per subsystem or kernel wide. (STATUS RETURNS)
 */

#ifndef MTSTATUS_H
#define MTSTATUS_H

#include <stdint.h>

typedef int32_t MTSTATUS;

/// Macros to test status
#define MT_SUCCEEDED(Status) ((Status) >= 0)
#define MT_FAILURE(Status)    ((Status) < 0)

//
// ==========================
// GENERAL MTSTATUS
// ==========================
#define MT_SUCCESS              ((MTSTATUS)0x00000000L)
#define MT_NOT_IMPLEMENTED      ((MTSTATUS)0xC0000001L)
#define MT_INVALID_PARAM        ((MTSTATUS)0xC0000002L)
#define MT_INVALID_STATE        ((MTSTATUS)0xC0000003L)
#define MT_ACCESS_DENIED        ((MTSTATUS)0xC0000004L)
#define MT_TIMEOUT              ((MTSTATUS)0xC0000005L)
#define MT_UNSUPPORTED_OP       ((MTSTATUS)0xC0000006L)
#define MT_ALREADY_EXISTS       ((MTSTATUS)0xC0000007L)
#define MT_NOT_FOUND            ((MTSTATUS)0xC0000008L)
#define MT_GENERAL_FAILURE		((MTSTATUS)0xC0000009L)
#define MT_INVALID_LOCK			((MTSTATUS)0xC0000010L)

//
// ==========================
// MEMORY MTSTATUS
// ==========================
#define MT_NO_MEMORY            ((MTSTATUS)0xC1000001L)
#define MT_MEMORY_LIMIT         ((MTSTATUS)0xC1000002L)
#define MT_PAGE_FAULT_ERROR     ((MTSTATUS)0xC1000003L)
#define MT_HEAP_CORRUPTION      ((MTSTATUS)0xC1000004L)
#define MT_INVALID_ADDRESS      ((MTSTATUS)0xC1000005L)

//
// ==========================
// VIRTUAL FILESYSTEM MTSTATUS
// ==========================
#define MT_IO_ERROR						((MTSTATUS)0xC2000001L)
#define MT_VFS_CORRUPTED				((MTSTATUS)0xC2000002L)
#define MT_VFS_READ_ONLY				((MTSTATUS)0xC2000003L)
#define MT_VFS_NO_SPACE					((MTSTATUS)0xC2000004L)
#define MT_VFS_PERMISSION_DENIED		((MTSTATUS)0xC2000005L)
#define MT_VFS_INITIALIZATION_FAILURE	((MTSTATUS)0xC2000006L)
#define MT_VFS_GENERAL_FAILURE			((MTSTATUS)0xC2000007L)

//
// FAT32-specific MTSTATUS
//
#define MT_FAT32_CLUSTERS_FULL      ((MTSTATUS)0xC2010001L) // No free clusters left
#define MT_FAT32_INVALID_CLUSTER    ((MTSTATUS)0xC2010002L) // Invalid cluster reference
#define MT_FAT32_DIR_FULL           ((MTSTATUS)0xC2010003L) // Directory has no free entries
#define MT_FAT32_FILE_NOT_FOUND     ((MTSTATUS)0xC2010004L)
#define MT_FAT32_PATH_TOO_LONG      ((MTSTATUS)0xC2010005L)
#define MT_FAT32_INVALID_FILENAME   ((MTSTATUS)0xC2010006L)
#define MT_FAT32_EOF                ((MTSTATUS)0xC2010007L) // End of file reached
#define MT_FAT32_DIRECTORY_ALREADY_EXISTS ((MTSTATUS)0xC2010008L) // The specified directory already exists in the path.
#define MT_FAT32_PARENT_PATH_NOT_FOUND ((MTSTATUS)0xC2010009L) // The directory's parent path has not been found.
#define MT_FAT32_PARENT_PATH_NOT_DIR ((MTSTATUS)0xC2010010L) // The directory's parent path is not a directory.
#define MT_FAT32_INVALID_WRITE_MODE ((MTSTATUS)0xC2010011L) // The write mode given to the function is invalid. (Not in FAT32_WRITE_MODE enum)
#define MT_FAT32_CLUSTER_NOT_FOUND ((MTSTATUS)0xC2010012L) // The directory's / file cluster couldn't have been found.
#define MT_FAT32_CLUSTER_GENERAL_FAILURE ((MTSTATUS)0xC2010013L) // General failure on a cluster operation.
#define MT_FAT32_DIRECTORY_NOT_FOUND ((MTSTATUS)0xC2010014L) // FAT32 Directory not found.

//
// ==========================
// DRIVER / DEVICE MTSTATUS
// ==========================
#define MT_DEVICE_NOT_READY     ((MTSTATUS)0xC3000001L)
#define MT_DEVICE_ERROR         ((MTSTATUS)0xC3000002L)
#define MT_DEVICE_TIMEOUT       ((MTSTATUS)0xC3000003L)
#define MT_DEVICE_UNSUPPORTED   ((MTSTATUS)0xC3000004L)
#define MT_AHCI_INIT_FAILED     ((MTSTATUS)0xC3010001L)
#define MT_AHCI_PORT_FAILURE    ((MTSTATUS)0xC3010002L)
#define MT_AHCI_READ_FAILURE	((MTSTATUS)0xC3010003L)
#define MT_AHCI_WRITE_FAILURE	((MTSTATUS)0xC3010004L)
#define MT_AHCI_TIMEOUT			((MTSTATUS)0xC3010005L)
#define MT_AHCI_GENERAL_FAILURE ((MTSTATUS)0xC3010006L)

//
// ==========================
// THREAD / SCHEDULER MTSTATUS
// ==========================
#define MT_THREAD_NOT_FOUND     ((MTSTATUS)0xC4000001L)
#define MT_THREAD_CREATION_FAIL ((MTSTATUS)0xC4000002L)
#define MT_SCHEDULER_ERROR      ((MTSTATUS)0xC4000003L)
#define MT_INVALID_IRQL         ((MTSTATUS)0xC4000004L)

//
// ==========================
// MUTEX MTSTATUS
// ==========================
#define MT_MUTEX_ALREADY_OWNED ((MTSTATUS)0xC5000001L)
#define MT_MUTEX_NOT_OWNED	   ((MTSTATUS)0xC5000002L)

//
// ==========================
// EVENT MTSTATUS
// ==========================
#define MT_EVENT_ALREADY_SIGNALED ((MTSTATUS)0xC6000001L)

#endif // MTSTATUS_H