/*++

Module Name:

	MatanelOS.h

Purpose:

	This header contains the prototypes, structures, enumerators, and functions required for typical user mode executable operation.

Author:

	slep (Matanel) 2025.

Revision History:

--*/

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
	const char* module_name;
	const char* function_name;
	void** func_ptr_addr;
} MT_IMPORT;

/* * MACRO: MT_IMPORT
 * Usage: MT_IMPORT("libname", VariableName)
 * * Pre-requisite: You must declare 'VariableName' yourself before calling this.
 */
#define MT_IMPORT(lib, func_name) \
    __attribute__((section(".mtimports"), used)) \
    const MT_IMPORT __import_entry_##func_name = { \
        .module_name = lib, \
        .function_name = #func_name, \
        .func_ptr_addr = (void**)&func_name \
    };

// Access rights
//
// Thread Access Rights
//
#define MT_THREAD_TERMINATE          0x0001    // Terminate the thread
#define MT_THREAD_SUSPEND_RESUME     0x0002    // Suspend or resume thread execution
#define MT_THREAD_SET_CONTEXT        0x0004    // Modify thread CPU context (registers, e.g RIP/RSP)
#define MT_THREAD_GET_CONTEXT        0x0008    // Read thread CPU context
#define MT_THREAD_QUERY_INFO         0x0010    // Query thread info (state, priority, etc.)
#define MT_THREAD_SET_INFO           0x0020    // Modify thread info (priority, name, affinity)

#define MT_THREAD_ALL_ACCESS         0x003F    // Request all valid thread access rights


//
// Process Access Rights
//
#define MT_PROCESS_TERMINATE          0x0001  // Kill the process
#define MT_PROCESS_CREATE_THREAD      0x0002  // Create a new thread inside process
#define MT_PROCESS_VM_OPERATION       0x0004  // Allocate/Protect/Free process memory
#define MT_PROCESS_VM_READ            0x0008  // Read from process memory
#define MT_PROCESS_VM_WRITE           0x0010  // Write to process memory
#define MT_PROCESS_DUP_HANDLE         0x0020  // Duplicate a handle into this process
#define MT_PROCESS_SET_INFO           0x0040  // Modify process properties/metadata
#define MT_PROCESS_QUERY_INFO         0x0080  // Query process details (PID, exit code, etc.)
#define MT_PROCESS_SUSPEND_RESUME     0x0100  // Suspend / Resume process
#define MT_PROCESS_CREATE_PROCESS     0x0200  // Create a new process.

#define MT_PROCESS_ALL_ACCESS         0x01FF  // Everything above

//
// File Access Rights
//
#define MT_FILE_READ_DATA            0x0001  // file & pipe
#define MT_FILE_LIST_DIRECTORY       0x0001  // directory

#define MT_FILE_WRITE_DATA           0x0002  // file & pipe
#define MT_FILE_ADD_FILE             0x0002  // directory

#define MT_FILE_APPEND_DATA          0x0004  // file
#define MT_FILE_ADD_SUBDIRECTORY     0x0004  // directory
#define MT_FILE_CREATE_PIPE_INSTANCE 0x0004  // named pipe

#define MT_FILE_READ_EA              0x0008  // file & directory
#define MT_FILE_WRITE_EA             0x0010  // file & directory

#define MT_FILE_EXECUTE              0x0020  // file
#define MT_FILE_TRAVERSE             0x0020  // directory

#define MT_FILE_DELETE_CHILD         0x0040  // directory

#define MT_FILE_READ_ATTRIBUTES      0x0080  // all
#define MT_FILE_WRITE_ATTRIBUTES     0x0100  // all
#define MT_FILE_ALL_ACCESS           0x01FF  // everything above

#define MT_FILE_GENERIC_READ  ( MT_FILE_READ_DATA    | MT_FILE_READ_ATTRIBUTES | MT_FILE_READ_EA )
#define MT_FILE_GENERIC_WRITE ( MT_FILE_WRITE_DATA   | MT_FILE_WRITE_ATTRIBUTES | MT_FILE_WRITE_EA | MT_FILE_APPEND_DATA )
#define MT_FILE_GENERIC_EXECUTE ( MT_FILE_READ_ATTRIBUTES | MT_FILE_EXECUTE )
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
#define MT_NO_RESOURCES			((MTSTATUS)0xC0000010L)
#define MT_INVALID_CHECK		((MTSTATUS)0xC0000011L)
#define MT_TYPE_MISMATCH		((MTSTATUS)0xC0000012L)
#define MT_OBJECT_DELETED		((MTSTATUS)0xC0000013L)
#define MT_INVALID_HANDLE		((MTSTATUS)0xC0000014L)

//
// ==========================
// MEMORY MTSTATUS
// ==========================
#define MT_NO_MEMORY            ((MTSTATUS)0xC1000001L)
#define MT_MEMORY_LIMIT         ((MTSTATUS)0xC1000002L)
#define MT_PAGE_FAULT_ERROR     ((MTSTATUS)0xC1000003L)
#define MT_HEAP_CORRUPTION      ((MTSTATUS)0xC1000004L)
#define MT_INVALID_ADDRESS      ((MTSTATUS)0xC1000005L)
#define MT_CONFLICTING_ADDRESSES ((MTSTATUS)0xC1000006L)
#define MT_INVALID_IMAGE_FORMAT ((MTSTATUS)0xC1000007L)

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
// ==========================
// FAT32-specific MTSTATUS
// ==========================
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
#define MT_FAT32_FILENAME_TOO_LONG	((MTSTATUS)0xC2010015L) // FAT32 Filename too long..

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
#define MT_THREAD_CREATION_FAILURE ((MTSTATUS)0xC4000002L)
#define MT_SCHEDULER_ERROR      ((MTSTATUS)0xC4000003L)
#define MT_INVALID_IRQL         ((MTSTATUS)0xC4000004L)

//
// ==========================
// MUTEX MTSTATUS
// ==========================
#define MT_MUTEX_ALREADY_OWNED ((MTSTATUS)0xC5000001L)
#define MT_MUTEX_NOT_OWNED	   ((MTSTATUS)0xC5000002L)
#define MT_INVALID_LOCK		   ((MTSTATUS)0xC500003L)

//
// ==========================
// EVENT MTSTATUS
// ==========================
#define MT_EVENT_ALREADY_SIGNALED ((MTSTATUS)0xC6000001L)

//
// ==========================
// PROCESS MTSTATUS
// ==========================
#define MT_PROCESS_IS_TERMINATING ((MTSTATUS)0xC7000000L)
#define MT_NOTHING_TO_TERMINATE ((MTSTATUS)0xC7000001L)

//
// ==========================
// EXCEPTION MTSTATUSES
// ==========================
#define MT_ACCESS_VIOLATION ((MTSTATUS)0xC8000000L)
#define MT_GUARD_PAGE_VIOLATION ((MTSTATUS)0xC8000001L)
#define MT_ILLEGAL_INSTRUCTION ((MTSTATUS)0xC8000002L)
#define MT_PRIVILEGED_INSTRUCTION ((MTSTATUS)0xC8000003L)
#define MT_DATATYPE_MISALIGNMENT ((MTSTATUS)0xC8000004L)
#define MT_INTEGER_DIVIDE_BY_ZERO ((MTSTATUS)0xC8000005L)

//
// ==========================
// USER MODE MTSTATUS
// ==========================
#define MT_INVALID_SYSTEM_SERVICE ((MTSTATUS)0xC9000000L)
#define MT_CANT_TERMINATE_SELF	  ((MTSTATUS)0xC9000001L)

// Imported functions from MTDLL that are needed for user mode executable operation:
// Basic definitions.
typedef int32_t HANDLE, * PHANDLE;
typedef uint32_t ACCESS_MASK;
#define IN // Takes REQUIRED INPUT
#define OUT // Supplies REQUIRED OUTPUT
#define _In_Opt // Takes OPTIONAL INPUT if given.
#define _Out_Opt // OPTIONALLY Supplies OUTPUT if given.
#define MtCurrentProcess() -1 // Special handle signifying current process.
#define MtCurrentThread() -2 // Special handle signifying current thread.

typedef enum _USER_ALLOCATION_TYPE {
	PAGE_EXECUTE_READ = 0x10, // PRESENT
	PAGE_EXECUTE_READWRITE = 0x20, // PRESENT | RW
	PAGE_READWRITE = 0x30, // PRESENT | RW | NX
	PAGE_READONLY = 0x40 // PRESENT | NX
} USER_ALLOCATION_TYPE;

extern char* (*strchr)(const char* s, int c);
extern char* (*strncat)(char* dest, const char* src, size_t max_len);
extern int   (*strncmp)(const char* s1, const char* s2, size_t length);
extern int   (*strcmp)(const char* s1, const char* s2);
extern char* (*strncpy)(char* dst, const char* src, size_t n);
extern char* (*strcpy)(char* dst, const char* src);
extern size_t(*strlen)(const char* str);

extern bool (*TerminateThread)(
    IN HANDLE ThreadHandle,
    IN uint32_t ExitStatus
    );

extern HANDLE(*OpenProcess)(
    IN  ACCESS_MASK DesiredAccess,
    IN  uint32_t ProcessId
    );

extern bool (*TerminateProcess)(
    IN  HANDLE ProcessHandle,
    IN  uint32_t ExitCode
    );

extern void* (*VirtualAlloc)(
    _In_Opt _Out_Opt void** BaseAddress,
    IN size_t AllocationSize,
    IN USER_ALLOCATION_TYPE AllocationType
    );

extern void* (*VirtualAllocEx)(
    IN HANDLE ProcessHandle,
    _In_Opt _Out_Opt void** BaseAddress,
    IN size_t AllocationSize,
    IN USER_ALLOCATION_TYPE AllocationType
    );

extern HANDLE(*CreateFile)(
    IN  const char* FileName,
    IN  ACCESS_MASK DesiredAccess
    );

extern bool (*WriteFile)(
    IN HANDLE FileHandle,
    IN uint32_t FileOffset,
    IN void* Buffer,
    IN size_t BufferSize,
    _Out_Opt size_t* BytesWritten
    );

extern bool (*ReadFile)(
    IN HANDLE FileHandle,
    IN uint32_t FileOffset,
    OUT void* Buffer,
    IN size_t BufferSize,
    _Out_Opt size_t* BytesRead
    );