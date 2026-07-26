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
#include "mtapi.h"

// Include the public MatanelOS definitions. MTSTATUS remains opt-in.
#include "../../shared/include/MatanelOS.h"
#include "errorhandlingapi.h"

// Internal ones have to be added manually by user, like in Windows.

// Imported functions from MTDLL that are needed for user mode executable operation:
// Basic definitions.
typedef int32_t HANDLE, * PHANDLE;
typedef uint32_t ACCESS_MASK;
#define MtCurrentProcess() -1 // Special handle signifying current process.
#define MtCurrentThread() -2 // Special handle signifying current thread.

typedef enum _USER_PROTECTION_TYPE {
	PAGE_EXECUTE_READ = 0x10, // PRESENT
	PAGE_EXECUTE_READWRITE = 0x20, // PRESENT | RW
	PAGE_READWRITE = 0x30, // PRESENT | RW | NX
	PAGE_READONLY = 0x40 // PRESENT | NX
} USER_PROTECTION_TYPE;

typedef enum _FREE_TYPE {
    MEM_RELEASE, // Release the entire region, base address must be the same that returned from MtAllocateVirtualMemory
    MEM_DECOMMIT // Decommit the region specified by the NumberOfBytes argument.
} FREE_TYPE;

typedef struct _MEMORY_BASIC_INFORMATION {
    void* BaseAddress;
    size_t RegionSize;
    USER_PROTECTION_TYPE Protection;
} MEMORY_BASIC_INFORMATION, * PMEMORY_BASIC_INFORMATION;

typedef uint32_t(*THREAD_START_ROUTINE)(void* Argument);

MTDLL_API char* strchr(const char* s, int c);
MTDLL_API char* strncat(char* dest, const char* src, size_t max_len);
MTDLL_API int strncmp(const char* s1, const char* s2, size_t length);
MTDLL_API int strcmp(const char* s1, const char* s2);
MTDLL_API char* strncpy(char* dst, const char* src, size_t n);
MTDLL_API char* strcpy(char* dst, const char* src);
MTDLL_API size_t strlen(const char* str);

MTDLL_API bool
CloseHandle(
    IN HANDLE ObjectHandle
);

MTDLL_API bool
TerminateThread(
    IN HANDLE ThreadHandle,
    IN uint32_t ExitStatus
);

MTDLL_API HANDLE
CreateThread(
    IN THREAD_START_ROUTINE StartRoutine,
    IN void* ThreadParameter
);

MTDLL_API HANDLE
CreateRemoteThread(
    IN HANDLE ProcessHandle,
    IN THREAD_START_ROUTINE StartRoutine,
    IN void* ThreadParameter
);

MTDLL_API HANDLE
OpenProcess(
    IN  ACCESS_MASK DesiredAccess,
    IN  uint32_t ProcessId
);

MTDLL_API bool
TerminateProcess(
    IN  HANDLE ProcessHandle,
    IN  uint32_t ExitCode
);

MTDLL_API void*
VirtualAlloc(
    _In_Opt _Out_Opt void** BaseAddress,
    IN size_t AllocationSize,
    IN USER_PROTECTION_TYPE AllocationType
);

MTDLL_API void*
VirtualAllocEx(
    IN HANDLE ProcessHandle,
    _In_Opt _Out_Opt void** BaseAddress,
    IN size_t AllocationSize,
    IN USER_PROTECTION_TYPE AllocationType
);

MTDLL_API bool
VirtualQuery(
    IN void* BaseAddress,
    OUT PMEMORY_BASIC_INFORMATION MemoryInformation
);

MTDLL_API bool
VirtualQueryEx(
    IN HANDLE ProcessHandle,
    IN void* BaseAddress,
    OUT PMEMORY_BASIC_INFORMATION MemoryInformation
);


MTDLL_API bool
VirtualProtect(
    IN void* BaseAddress,
    IN size_t RegionSize,
    IN USER_PROTECTION_TYPE NewProtection,
    OUT USER_PROTECTION_TYPE* OldProtection
);

MTDLL_API bool
VirtualProtectEx(
    IN HANDLE ProcessHandle,
    IN void* BaseAddress,
    IN size_t RegionSize,
    IN USER_PROTECTION_TYPE NewProtection,
    OUT USER_PROTECTION_TYPE* OldProtection
);

MTDLL_API bool
VirtualFree(
    IN void* BaseAddress,
    IN size_t NumberOfBytes,
    IN FREE_TYPE FreeType
);

MTDLL_API bool
VirtualFreeEx(
    IN HANDLE ProcessHandle,
    IN void* BaseAddress,
    IN size_t NumberOfBytes,
    IN FREE_TYPE FreeType
);

MTDLL_API HANDLE
CreateFile(
    IN  const char* FileName,
    IN  ACCESS_MASK DesiredAccess
);

MTDLL_API bool
WriteFile(
    IN HANDLE FileHandle,
    IN uint32_t FileOffset,
    IN void* Buffer,
    IN size_t BufferSize,
    _Out_Opt size_t* BytesWritten
);

MTDLL_API bool
ReadFile(
    IN HANDLE FileHandle,
    IN uint32_t FileOffset,
    OUT void* Buffer,
    IN size_t BufferSize,
    _Out_Opt size_t* BytesRead
);

MTDLL_API void
Sleep(
    IN uint32_t Milliseconds
);

MTDLL_API uint32_t
WaitForSingleObject(
    IN HANDLE ObjectHandle,
    IN uint32_t Milliseconds
);

MTDLL_API uint32_t
WaitForSingleObjectEx(
    IN HANDLE ObjectHandle,
    IN uint32_t Milliseconds,
    IN bool Alertable
);

MTDLL_API HANDLE
CreateEvent(
    IN EVENT_TYPE EventType,
    IN bool InitialState,
    _In_Opt const char* Name
);

MTDLL_API bool
QueryEvent(
    IN HANDLE EventHandle,
    OUT bool* SignalState
);

MTDLL_API bool SetEvent(IN HANDLE EventHandle);
MTDLL_API bool ResetEvent(IN HANDLE EventHandle);

MTDLL_API HANDLE
CreateMutex(
    IN bool InitialOwner,
    _In_Opt const char* Name
);

MTDLL_API bool
QueryMutex(
    IN HANDLE MutexHandle,
    OUT PMUTEX_BASIC_INFORMATION Information
);

MTDLL_API bool ReleaseMutex(IN HANDLE MutexHandle);

MTDLL_API HANDLE
CreateSemaphore(
    IN int32_t InitialCount,
    IN int32_t MaximumCount,
    _In_Opt const char* Name
);

MTDLL_API bool
QuerySemaphore(
    IN HANDLE SemaphoreHandle,
    OUT PSEMAPHORE_BASIC_INFORMATION Information
);

MTDLL_API bool
ReleaseSemaphore(
    IN HANDLE SemaphoreHandle,
    IN int32_t ReleaseCount,
    _Out_Opt int32_t* PreviousCount
);

MTDLL_API void printf(uint32_t Color, const char* fmt, ...);
