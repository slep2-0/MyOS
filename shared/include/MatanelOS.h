/*
 * PROJECT:      MatanelOS
 * LICENSE:      GPLv3
 * PURPOSE:      Canonical stable public MatanelOS API.
 *
 * MTDLL and user applications include this same header. Native MTSTATUS-based
 * services remain opt-in through mtnative.h because that ABI may change while
 * the operating system is under development.
 */

#ifndef MATANELOS_SHARED_MATANELOS_H
#define MATANELOS_SHARED_MATANELOS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "accessrights.h"
#include "annotations.h"
#include "errorcodes.h"
#include "fileapi.h"
#include "mtapi.h"
#include "mttypes.h"
#include "synchapi.h"
#include "mtexception.h"
#include "mtlanguage.h"
#include "heapapi.h"
#include "libloaderapi.h"

#ifdef __cplusplus
extern "C" {
#endif

// Prevents CPU Reordering as well as the MmBarrier functionality.
#define MmFullBarrier() __sync_synchronize()

// Ensure ordering of memory operations (memory should be visible before continuing)
#define MmBarrier() __asm__ __volatile__("mfence" ::: "memory")

MTDLL_API char* strchr(const char* String, int Character);

MTDLL_API char* strncat(char* Destination, const char* Source, size_t MaximumLength);

MTDLL_API int strncmp(const char* First, const char* Second, size_t Length);

MTDLL_API int strcmp(const char* First, const char* Second);

MTDLL_API char* strncpy(char* Destination, const char* Source, size_t Length);

MTDLL_API char* strcpy(char* Destination, const char* Source);

MTDLL_API size_t strlen(const char* String);

MTDLL_API int isspace(int c);

MTDLL_API
void*
memcpy(
    void* Destination,
    const void* Source,
    size_t Size
);

MTDLL_API
void*
memset(
    void* Destination,
    int Value,
    size_t Size
);

MTDLL_API ERROR_CODE
GetLastError(
    void
);

MTDLL_API void
SetLastError(
    IN ERROR_CODE ErrorCode
);

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
    IN ACCESS_MASK DesiredAccess,
    IN uint32_t ProcessId
);

MTDLL_API bool
TerminateProcess(
    IN HANDLE ProcessHandle,
    IN uint32_t ExitCode
);

MTDLL_API bool
GetExitCodeProcess(
    IN HANDLE ProcessHandle,
    OUT uint32_t* ExitCode
);

MTDLL_API bool
GetExitCodeThread(
    IN HANDLE ThreadHandle,
    OUT uint32_t* ExitCode
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
    IN const char* FileName,
    IN ACCESS_MASK DesiredAccess,
    IN FILE_CREATION_DISPOSITION CreationDisposition
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
    IN uint64_t Milliseconds
);

MTDLL_API uint32_t
WaitForSingleObjectEx(
    IN HANDLE ObjectHandle,
    IN uint64_t Milliseconds,
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

MTDLL_API bool
SetEvent(
    IN HANDLE EventHandle
);

MTDLL_API bool
ResetEvent(
    IN HANDLE EventHandle
);

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

MTDLL_API bool
ReleaseMutex(
    IN HANDLE MutexHandle
);

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

MTDLL_API uint32_t
SuspendThread(
    IN HANDLE ThreadHandle
);

MTDLL_API uint32_t
ResumeThread(
    IN HANDLE ThreadHandle
);

MTDLL_API void
RaiseException(
    IN uint32_t ExceptionCode,
    IN uint32_t ExceptionFlags,
    IN uint32_t NumberOfArguments,
    _In_Opt const uintptr_t* Arguments
);

MTDLL_API void
printf(
    IN uint32_t Color,
    IN const char* Format,
    ...
);

#ifdef __cplusplus
}
#endif

#endif /* MATANELOS_SHARED_MATANELOS_H */
