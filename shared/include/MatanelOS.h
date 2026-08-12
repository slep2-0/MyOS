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

/*++

    Routine description:

        Searches a string for its first occurrence of a character.

    Arguments:

        [IN] String - The string to search.
        [IN] Character - The character to find.

    Return Values:

        A pointer to the matching character, or NULL when no match exists.

--*/
MTDLL_API char* strchr(const char* String, int Character);

/*++

    Routine description:

        Appends source characters to a bounded destination string.

    Arguments:

        [IN OUT] Destination - The destination string.
        [IN] Source - The source string.
        [IN] MaximumLength - The destination bound including its terminator.

    Return Values:

        Destination.

--*/
MTDLL_API char* strncat(char* Destination, const char* Source, size_t MaximumLength);

/*++

    Routine description:

        Compares at most Length characters from two strings.

    Arguments:

        [IN] First - The first string.
        [IN] Second - The second string.
        [IN] Length - The maximum comparison length.

    Return Values:

        A negative, zero, or positive comparison result.

--*/
MTDLL_API int strncmp(const char* First, const char* Second, size_t Length);

/*++

    Routine description:

        Compares two null-terminated strings.

    Arguments:

        [IN] First - The first string.
        [IN] Second - The second string.

    Return Values:

        A negative, zero, or positive comparison result.

--*/
MTDLL_API int strcmp(const char* First, const char* Second);

/*++

    Routine description:

        Copies at most Length - 1 characters and terminates the destination
        when Length is nonzero.

    Arguments:

        [OUT] Destination - The destination buffer.
        [IN] Source - The source string.
        [IN] Length - The destination length limit.

    Return Values:

        Destination.

--*/
MTDLL_API char* strncpy(char* Destination, const char* Source, size_t Length);

/*++

    Routine description:

        Copies a null-terminated string to a destination buffer.

    Arguments:

        [OUT] Destination - The destination buffer.
        [IN] Source - The source string.

    Return Values:

        Destination.

--*/
MTDLL_API char* strcpy(char* Destination, const char* Source);

/*++

    Routine description:

        Returns the number of characters before a string terminator.

    Arguments:

        [IN] String - The string to measure.

    Return Values:

        The string length in characters.

--*/
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

/*++

    Routine description:

        Retrieves the last error value for the current thread.

    Arguments:

        None.

    Return Values:

        The current thread's ERROR_CODE value.

--*/
MTDLL_API ERROR_CODE
GetLastError(
    void
);

/*++

    Routine description:

        Stores an error value for the current thread.

    Arguments:

        [IN] ErrorCode - The error value to store.

    Return Values:

        None.

--*/
MTDLL_API void
SetLastError(
    IN ERROR_CODE ErrorCode
);

/*++

    Routine description:

        Closes a user-mode object handle.

    Arguments:

        [IN] ObjectHandle - The handle to close.

    Return Values:

        true on success, or false when the handle is invalid or cannot be
        closed.

--*/
MTDLL_API bool
CloseHandle(
    IN HANDLE ObjectHandle
);

/*++

    Routine description:

        Requests termination of a thread.

    Arguments:

        [IN] ThreadHandle - The thread handle.
        [IN] ExitStatus - The status assigned on termination.

    Return Values:

        true when termination is requested, or false on failure.

--*/
MTDLL_API bool
TerminateThread(
    IN HANDLE ThreadHandle,
    IN uint32_t ExitStatus
);

/*++

    Routine description:

        Creates a thread in the current process.

    Arguments:

        [IN] StartRoutine - The new thread's start routine.
        [IN] ThreadParameter - The value passed to StartRoutine.

    Return Values:

        A thread handle, or MT_INVALID_HANDLE on failure.

--*/
MTDLL_API HANDLE
CreateThread(
    IN THREAD_START_ROUTINE StartRoutine,
    IN void* ThreadParameter
);

/*++

    Routine description:

        Creates a thread in a target process.

    Arguments:

        [IN] ProcessHandle - The target process handle.
        [IN] StartRoutine - The new thread's start routine.
        [IN] ThreadParameter - The value passed to StartRoutine.

    Return Values:

        A thread handle, or MT_INVALID_HANDLE on failure.

--*/
MTDLL_API HANDLE
CreateRemoteThread(
    IN HANDLE ProcessHandle,
    IN THREAD_START_ROUTINE StartRoutine,
    IN void* ThreadParameter
);

/*++

    Routine description:

        Opens a process handle with requested access.

    Arguments:

        [IN] DesiredAccess - The requested process access mask.
        [IN] ProcessId - The process identifier.

    Return Values:

        A process handle, or MT_INVALID_HANDLE on failure.

--*/
MTDLL_API HANDLE
OpenProcess(
    IN ACCESS_MASK DesiredAccess,
    IN uint32_t ProcessId
);

/*++

    Routine description:

        Requests termination of a process.

    Arguments:

        [IN] ProcessHandle - The process handle.
        [IN] ExitCode - The process exit code.

    Return Values:

        true when termination is requested, or false on failure.

--*/
MTDLL_API bool
TerminateProcess(
    IN HANDLE ProcessHandle,
    IN uint32_t ExitCode
);

/*++

    Routine description:

        Retrieves a process exit code.

    Arguments:

        [IN] ProcessHandle - The process handle.
        [OUT] ExitCode - Receives the process exit code.

    Return Values:

        true on success, or false on failure.

--*/
MTDLL_API bool
GetExitCodeProcess(
    IN HANDLE ProcessHandle,
    OUT uint32_t* ExitCode
);

/*++

    Routine description:

        Retrieves a thread exit code.

    Arguments:

        [IN] ThreadHandle - The thread handle.
        [OUT] ExitCode - Receives the thread exit code.

    Return Values:

        true on success, or false on failure.

--*/
MTDLL_API bool
GetExitCodeThread(
    IN HANDLE ThreadHandle,
    OUT uint32_t* ExitCode
);

/*++

    Routine description:

        Allocates virtual memory in the current process.

    Arguments:

        [IN OUT OPTIONAL] BaseAddress - The preferred or returned base address.
        [IN] AllocationSize - The number of bytes to allocate.
        [IN] AllocationType - The requested page protection.

    Return Values:

        The allocated base address, or NULL on failure.

--*/
MTDLL_API void*
VirtualAlloc(
    _In_Opt _Out_Opt void** BaseAddress,
    IN size_t AllocationSize,
    IN USER_PROTECTION_TYPE AllocationType
);

/*++

    Routine description:

        Allocates virtual memory in a target process.

    Arguments:

        [IN] ProcessHandle - The target process handle.
        [IN OUT OPTIONAL] BaseAddress - The preferred or returned base address.
        [IN] AllocationSize - The number of bytes to allocate.
        [IN] AllocationType - The requested page protection.

    Return Values:

        The allocated base address, or NULL on failure.

--*/
MTDLL_API void*
VirtualAllocEx(
    IN HANDLE ProcessHandle,
    _In_Opt _Out_Opt void** BaseAddress,
    IN size_t AllocationSize,
    IN USER_PROTECTION_TYPE AllocationType
);

/*++

    Routine description:

        Queries the virtual memory region containing an address.

    Arguments:

        [IN] BaseAddress - The address to query.
        [OUT] MemoryInformation - Receives region information.

    Return Values:

        true on success, or false on failure.

--*/
MTDLL_API bool
VirtualQuery(
    IN void* BaseAddress,
    OUT PMEMORY_BASIC_INFORMATION MemoryInformation
);

/*++

    Routine description:

        Queries virtual memory in a target process.

    Arguments:

        [IN] ProcessHandle - The target process handle.
        [IN] BaseAddress - The address to query.
        [OUT] MemoryInformation - Receives region information.

    Return Values:

        true on success, or false on failure.

--*/
MTDLL_API bool
VirtualQueryEx(
    IN HANDLE ProcessHandle,
    IN void* BaseAddress,
    OUT PMEMORY_BASIC_INFORMATION MemoryInformation
);

/*++

    Routine description:

        Changes protection for a region in the current process.

    Arguments:

        [IN] BaseAddress - The first address in the region.
        [IN] RegionSize - The region size in bytes.
        [IN] NewProtection - The new page protection.
        [OUT] OldProtection - Receives the previous protection.

    Return Values:

        true on success, or false on failure.

--*/
MTDLL_API bool
VirtualProtect(
    IN void* BaseAddress,
    IN size_t RegionSize,
    IN USER_PROTECTION_TYPE NewProtection,
    OUT USER_PROTECTION_TYPE* OldProtection
);

/*++

    Routine description:

        Changes protection for a region in a target process.

    Arguments:

        [IN] ProcessHandle - The target process handle.
        [IN] BaseAddress - The first address in the region.
        [IN] RegionSize - The region size in bytes.
        [IN] NewProtection - The new page protection.
        [OUT] OldProtection - Receives the previous protection.

    Return Values:

        true on success, or false on failure.

--*/
MTDLL_API bool
VirtualProtectEx(
    IN HANDLE ProcessHandle,
    IN void* BaseAddress,
    IN size_t RegionSize,
    IN USER_PROTECTION_TYPE NewProtection,
    OUT USER_PROTECTION_TYPE* OldProtection
);

/*++

    Routine description:

        Releases virtual memory in the current process.

    Arguments:

        [IN] BaseAddress - The allocation base address.
        [IN] NumberOfBytes - The requested release size.
        [IN] FreeType - The release operation.

    Return Values:

        true on success, or false on failure.

--*/
MTDLL_API bool
VirtualFree(
    IN void* BaseAddress,
    IN size_t NumberOfBytes,
    IN FREE_TYPE FreeType
);

/*++

    Routine description:

        Releases virtual memory in a target process.

    Arguments:

        [IN] ProcessHandle - The target process handle.
        [IN] BaseAddress - The allocation base address.
        [IN] NumberOfBytes - The requested release size.
        [IN] FreeType - The release operation.

    Return Values:

        true on success, or false on failure.

--*/
MTDLL_API bool
VirtualFreeEx(
    IN HANDLE ProcessHandle,
    IN void* BaseAddress,
    IN size_t NumberOfBytes,
    IN FREE_TYPE FreeType
);

/*++

    Routine description:

        Opens a file with the requested access.

    Arguments:

        [IN] FileName - The file path.
        [IN] DesiredAccess - The requested access mask.
        [IN] CreationDisposition - The file creation flags.

    Return Values:

        A file handle, or MT_INVALID_HANDLE on failure.

--*/
MTDLL_API HANDLE
CreateFile(
    IN const char* FileName,
    IN ACCESS_MASK DesiredAccess,
    IN FILE_CREATION_DISPOSITION CreationDisposition
);

/*++

    Routine description:

        Writes bytes to an opened file.

    Arguments:

        [IN] FileHandle - The file handle.
        [IN] FileOffset - The byte offset.
        [IN] Buffer - The source buffer.
        [IN] BufferSize - The number of bytes to write.
        [OUT OPTIONAL] BytesWritten - Receives the number written.

    Return Values:

        true on success, or false on failure.

--*/
MTDLL_API bool
WriteFile(
    IN HANDLE FileHandle,
    IN uint32_t FileOffset,
    IN void* Buffer,
    IN size_t BufferSize,
    _Out_Opt size_t* BytesWritten
);

/*++

    Routine description:

        Reads bytes from an opened file.

    Arguments:

        [IN] FileHandle - The file handle.
        [IN] FileOffset - The byte offset.
        [OUT] Buffer - The destination buffer.
        [IN] BufferSize - The maximum number of bytes to read.
        [OUT OPTIONAL] BytesRead - Receives the number read.

    Return Values:

        true on success, or false on failure.

--*/
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

/*++

    Routine description:

        Waits for a dispatcher object without alertable APC delivery.

    Arguments:

        [IN] ObjectHandle - The object handle.
        [IN] Milliseconds - The maximum wait interval.

    Return Values:

        WAIT_OBJECT_0, WAIT_TIMEOUT, WAIT_ABANDONED_0, or WAIT_FAILED.

--*/
MTDLL_API uint32_t
WaitForSingleObject(
    IN HANDLE ObjectHandle,
    IN uint64_t Milliseconds
);

/*++

    Routine description:

        Waits for a dispatcher object with configurable alertability.

    Arguments:

        [IN] ObjectHandle - The object handle.
        [IN] Milliseconds - The maximum wait interval.
        [IN] Alertable - Whether APC alertability is enabled.

    Return Values:

        WAIT_OBJECT_0, WAIT_TIMEOUT, WAIT_ABANDONED_0, or WAIT_FAILED.

--*/
MTDLL_API uint32_t
WaitForSingleObjectEx(
    IN HANDLE ObjectHandle,
    IN uint64_t Milliseconds,
    IN bool Alertable
);

/*++

    Routine description:

        Creates an event object.

    Arguments:

        [IN] EventType - The event signaling semantics.
        [IN] InitialState - Whether the event starts signaled.
        [IN OPTIONAL] Name - The optional object name.

    Return Values:

        An event handle, or MT_INVALID_HANDLE on failure.

--*/
MTDLL_API HANDLE
CreateEvent(
    IN EVENT_TYPE EventType,
    IN bool InitialState,
    _In_Opt const char* Name
);

/*++

    Routine description:

        Queries an event's signal state.

    Arguments:

        [IN] EventHandle - The event handle.
        [OUT] SignalState - Receives the signal state.

    Return Values:

        true on success, or false on failure.

--*/
MTDLL_API bool
QueryEvent(
    IN HANDLE EventHandle,
    OUT bool* SignalState
);

/*++

    Routine description:

        Signals an event and releases eligible waiters.

    Arguments:

        [IN] EventHandle - The event handle.

    Return Values:

        true on success, or false on failure.

--*/
MTDLL_API bool
SetEvent(
    IN HANDLE EventHandle
);

/*++

    Routine description:

        Resets an event to its nonsignaled state.

    Arguments:

        [IN] EventHandle - The event handle.

    Return Values:

        true on success, or false on failure.

--*/
MTDLL_API bool
ResetEvent(
    IN HANDLE EventHandle
);

/*++

    Routine description:

        Creates a mutex object and optionally gives initial ownership to the
        current thread.

    Arguments:

        [IN] InitialOwner - Whether the current thread initially owns it.
        [IN OPTIONAL] Name - The optional object name.

    Return Values:

        A mutex handle, or MT_INVALID_HANDLE on failure.

--*/
MTDLL_API HANDLE
CreateMutex(
    IN bool InitialOwner,
    _In_Opt const char* Name
);

/*++

    Routine description:

        Queries basic mutex state.

    Arguments:

        [IN] MutexHandle - The mutex handle.
        [OUT] Information - Receives mutex information.

    Return Values:

        true on success, or false on failure.

--*/
MTDLL_API bool
QueryMutex(
    IN HANDLE MutexHandle,
    OUT PMUTEX_BASIC_INFORMATION Information
);

/*++

    Routine description:

        Releases mutex ownership held by the current thread.

    Arguments:

        [IN] MutexHandle - The mutex handle.

    Return Values:

        true on success, or false when ownership or the handle is invalid.

--*/
MTDLL_API bool
ReleaseMutex(
    IN HANDLE MutexHandle
);

/*++

    Routine description:

        Creates a semaphore with the requested initial and maximum counts.

    Arguments:

        [IN] InitialCount - The initial available count.
        [IN] MaximumCount - The greatest permitted count.
        [IN OPTIONAL] Name - The optional object name.

    Return Values:

        A semaphore handle, or MT_INVALID_HANDLE on failure.

--*/
MTDLL_API HANDLE
CreateSemaphore(
    IN int32_t InitialCount,
    IN int32_t MaximumCount,
    _In_Opt const char* Name
);

/*++

    Routine description:

        Queries basic semaphore state.

    Arguments:

        [IN] SemaphoreHandle - The semaphore handle.
        [OUT] Information - Receives semaphore information.

    Return Values:

        true on success, or false on failure.

--*/
MTDLL_API bool
QuerySemaphore(
    IN HANDLE SemaphoreHandle,
    OUT PSEMAPHORE_BASIC_INFORMATION Information
);

/*++

    Routine description:

        Adds a release count to a semaphore and releases eligible waiters.

    Arguments:

        [IN] SemaphoreHandle - The semaphore handle.
        [IN] ReleaseCount - The count to add.
        [OUT OPTIONAL] PreviousCount - Receives the count before release.

    Return Values:

        true on success, or false when the count would exceed its maximum.

--*/
MTDLL_API bool
ReleaseSemaphore(
    IN HANDLE SemaphoreHandle,
    IN int32_t ReleaseCount,
    _Out_Opt int32_t* PreviousCount
);

/*++

    Routine description:

        Increments a thread's suspension count.

    Arguments:

        [IN] ThreadHandle - The thread handle.

    Return Values:

        The previous suspension count, or UINT32_MAX on failure.

--*/
MTDLL_API uint32_t
SuspendThread(
    IN HANDLE ThreadHandle
);

/*++

    Routine description:

        Decrements a thread's suspension count.

    Arguments:

        [IN] ThreadHandle - The thread handle.

    Return Values:

        The previous suspension count, or UINT32_MAX on failure.

--*/
MTDLL_API uint32_t
ResumeThread(
    IN HANDLE ThreadHandle
);

/*++

    Routine description:

        Raises a user-mode exception for the current thread.

    Arguments:

        [IN] ExceptionCode - The exception code.
        [IN] ExceptionFlags - The exception flags.
        [IN] NumberOfArguments - The number of exception arguments.
        [IN OPTIONAL] Arguments - The exception argument array.

    Return Values:

        None. Exception dispatch continues through the user-mode exception
        path.

--*/
MTDLL_API void
RaiseException(
    IN uint32_t ExceptionCode,
    IN uint32_t ExceptionFlags,
    IN uint32_t NumberOfArguments,
    _In_Opt const uintptr_t* Arguments
);

/*++

    Routine description:

        Formats a message and writes it to the MatanelOS console.

    Arguments:

        [IN] Color - The console color.
        [IN] Format - The format string followed by its arguments.

    Return Values:

        None.

--*/
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
