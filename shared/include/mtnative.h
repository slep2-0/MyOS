/*
 * PROJECT:      MatanelOS
 * LICENSE:      GPLv3
 * PURPOSE:      Native MatanelOS system-service API.
 *
 * WARNING:
 *     This is the low-level, unstable MatanelOS ABI. Native declarations,
 *     structures, information classes, and behavior may change at any time
 *     while the operating system is under development. Applications that do
 *     not require native control should prefer the higher-level MatanelOS API.
 */

#ifndef MATANELOS_SHARED_MTNATIVE_H
#define MATANELOS_SHARED_MTNATIVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "accessrights.h"
#include "annotations.h"
#include "mtapi.h"
#include "mttypes.h"
#include "mtstatus.h"
#include "synchapi.h"
#include "mtexception.h"
#include "../../shared/include/fileapi.h"

/*
 * Native service stubs are provided by MTDLL. When MTDLL is built from C,
 * this annotation publishes a definition through the generated MTE export
 * directory. User programs see an ordinary external declaration, which the
 * MTE linker and packer turn into an import only when referenced.
 */
#define MTNATIVE_API MTDLL_API

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Native-only process information classes belong here beside the service
 * contract. Add new values explicitly; changing an existing value changes
 * the native ABI.
 */
typedef enum _PROCESSINFOCLASS {
    ProcessBasicInformation = 0
} PROCESSINFOCLASS;

typedef enum _THREADINFOCLASS {
    ThreadBasicInformation = 0
} THREADINFOCLASS;

struct _PEB;

typedef struct _PROCESS_BASIC_INFORMATION {
    MTSTATUS ExitStatus;
    struct _PEB* PebBaseAddress;
    HANDLE UniqueProcessId;
    HANDLE ParentUniqueProcessId;
} PROCESS_BASIC_INFORMATION;

typedef struct _THREAD_BASIC_INFORMATION {
    MTSTATUS ExitStatus;
    struct _TEB* TebBaseAddress;
    HANDLE UniqueThreadId;
    HANDLE UniqueProcessId;
} THREAD_BASIC_INFORMATION;

typedef struct _MT_CREATE_PROCESS_PARAMETERS {
    uint32_t Size;  // Size of this structure for native ABI versioning.
    uint32_t Flags; // Process creation options; currently required to be zero.

    const char* ImagePath;     // Executable file that the kernel opens and maps.
    uint64_t ImagePathLength;  // Image-path bytes, excluding the null terminator.

    const char* CommandLine;    // Raw child command line parsed by MTDLL into argc/argv.
    uint64_t CommandLineLength; // Command-line bytes, excluding the null terminator.

    const char* CurrentDirectory;    // Initial working-directory string published to the child.
    uint64_t CurrentDirectoryLength; // Directory bytes, excluding the null terminator.

    const char* Environment;  // Consecutive NAME=VALUE strings terminated by an empty string.
    uint64_t EnvironmentSize; // Total environment bytes, including the final two null bytes.

    HANDLE ParentProcess;      // Parent-process handle; normally MtCurrentProcess().
    ACCESS_MASK DesiredAccess; // Access rights requested for the returned process handle.
} MT_CREATE_PROCESS_PARAMETERS, * PMT_CREATE_PROCESS_PARAMETERS;

typedef PROCESS_INFORMATION MT_PROCESS_INFORMATION;
typedef PPROCESS_INFORMATION PMT_PROCESS_INFORMATION;

MTNATIVE_API MTSTATUS
MtAllocateVirtualMemory(
    IN HANDLE Process,
    _In_Opt _Out_Opt void** BaseAddress,
    IN size_t NumberOfBytes,
    IN uint8_t AllocationType
);

MTNATIVE_API MTSTATUS
MtOpenProcess(
    IN uint32_t ProcessId,
    OUT PHANDLE ProcessHandle,
    IN ACCESS_MASK DesiredAccess
);  

MTNATIVE_API MTSTATUS
MtTerminateProcess(
    IN HANDLE ProcessHandle,
    IN MTSTATUS ExitStatus
);

MTNATIVE_API MTSTATUS
MtReadFile(
    IN HANDLE FileHandle,
    IN uint64_t FileOffset,
    OUT void* Buffer,
    IN size_t BufferSize,
    _Out_Opt size_t* BytesRead
);

MTNATIVE_API MTSTATUS
MtWriteFile(
    IN HANDLE FileHandle,
    IN uint64_t FileOffset,
    IN void* Buffer,
    IN size_t BufferSize,
    _Out_Opt size_t* BytesWritten
);

MTNATIVE_API MTSTATUS
MtCreateFile(
    IN const char* Path,
    IN ACCESS_MASK DesiredAccess,
    IN FILE_CREATION_DISPOSITION CreationDisposition,
    OUT PHANDLE FileHandle
);

MTNATIVE_API MTSTATUS
MtClose(
    IN HANDLE ObjectHandle
);

MTNATIVE_API MTSTATUS
MtTerminateThread(
    IN HANDLE ThreadHandle,
    IN MTSTATUS ExitStatus
);

MTNATIVE_API MTSTATUS
MtQueryVirtualMemory(
    IN HANDLE ProcessHandle,
    IN void* BaseAddress,
    OUT PMEMORY_BASIC_INFORMATION MemoryInformation
);

MTNATIVE_API MTSTATUS
MtProtectVirtualMemory(
    IN HANDLE ProcessHandle,
    IN OUT void** BaseAddress,
    IN OUT size_t* RegionSize,
    IN USER_PROTECTION_TYPE NewProtection,
    OUT USER_PROTECTION_TYPE* OldProtection
);

MTNATIVE_API MTSTATUS
MtFreeVirtualMemory(
    IN HANDLE ProcessHandle,
    IN OUT void** BaseAddress,
    IN OUT size_t* NumberOfBytes,
    IN FREE_TYPE FreeType
);

MTNATIVE_API MTSTATUS
MtCreateThread(
    IN HANDLE ProcessHandle,
    IN THREAD_START_ROUTINE StartRoutine,
    IN void* Argument,
    OUT PHANDLE ThreadHandle
);

/*
 * Continues execution from a user APC using a validated user-mode context.
 * This service is only valid while the calling thread has an active user APC.
 */
MTNATIVE_API NORETURN void
MtContinue(
    IN const CONTEXT* ContextRecord
);

MTNATIVE_API MTSTATUS
MtDelayExecution(
    IN bool Alertable,
    IN uint64_t Milliseconds
);

MTNATIVE_API MTSTATUS
MtWaitForSingleObject(
    IN HANDLE ObjectHandle,
    IN uint64_t Milliseconds,
    IN bool Alertable
);

MTNATIVE_API MTSTATUS
MtCreateEvent(
    OUT PHANDLE EventHandle,
    IN ACCESS_MASK DesiredAccess,
    IN EVENT_TYPE EventType,
    IN bool InitialState,
    _In_Opt const char* Name
);

MTNATIVE_API MTSTATUS
MtQueryEvent(
    IN HANDLE EventHandle,
    OUT bool* SignalState
);

MTNATIVE_API MTSTATUS
MtSetEvent(
    IN HANDLE EventHandle,
    _Out_Opt bool* PreviousState
);

MTNATIVE_API MTSTATUS
MtResetEvent(
    IN HANDLE EventHandle,
    _Out_Opt bool* PreviousState
);

MTNATIVE_API MTSTATUS
MtCreateMutex(
    OUT PHANDLE MutexHandle,
    IN ACCESS_MASK DesiredAccess,
    IN bool InitialOwner,
    _In_Opt const char* Name
);

MTNATIVE_API MTSTATUS
MtQueryMutex(
    IN HANDLE MutexHandle,
    OUT PMUTEX_BASIC_INFORMATION Information
);

MTNATIVE_API MTSTATUS
MtReleaseMutex(
    IN HANDLE MutexHandle,
    _Out_Opt int32_t* PreviousCount
);

MTNATIVE_API MTSTATUS
MtCreateSemaphore(
    OUT PHANDLE SemaphoreHandle,
    IN ACCESS_MASK DesiredAccess,
    IN int32_t InitialCount,
    IN int32_t MaximumCount,
    _In_Opt const char* Name
);

MTNATIVE_API MTSTATUS
MtQuerySemaphore(
    IN HANDLE SemaphoreHandle,
    OUT PSEMAPHORE_BASIC_INFORMATION Information
);

MTNATIVE_API MTSTATUS
MtReleaseSemaphore(
    IN HANDLE SemaphoreHandle,
    IN int32_t ReleaseCount,
    _Out_Opt int32_t* PreviousCount
);

MTNATIVE_API MTSTATUS
MtQueryInformationProcess(
    IN HANDLE ProcessHandle,
    IN PROCESSINFOCLASS ProcessInformationClass,
    OUT void* ProcessInformation,
    IN size_t ProcessInformationLength,
    _Out_Opt uint32_t* ReturnLength
);

MTNATIVE_API MTSTATUS
MtQueryInformationThread(
    IN HANDLE ThreadHandle,
    IN THREADINFOCLASS ThreadInformationClass,
    OUT void* ThreadInformation,
    IN size_t ThreadInformationLength,
    _Out_Opt uint32_t* ReturnLength
);

MTNATIVE_API MTSTATUS
MtSuspendThread(
    IN HANDLE ThreadHandle,
    _Out_Opt uint32_t* PreviousSuspendCount
);

MTNATIVE_API MTSTATUS
MtResumeThread(
    IN HANDLE ThreadHandle,
    _Out_Opt uint32_t* PreviousSuspendCount
);

MTNATIVE_API MTSTATUS
MtRaiseException(
    IN const EXCEPTION_RECORD* ExceptionRecord
);

MTNATIVE_API MTSTATUS
MtCreateSection(
    OUT PHANDLE SectionHandle,
    IN ACCESS_MASK DesiredAccess,
    IN HANDLE FileHandle
);

MTNATIVE_API MTSTATUS
MtMapViewOfSection(
    IN HANDLE SectionHandle,
    IN HANDLE ProcessHandle,
    OUT void** BaseAddress,
    OUT void** EntryPointAddress,
    OUT size_t* ViewSize
);

MTNATIVE_API MTSTATUS
MtUnmapViewOfSection(
    IN HANDLE ProcessHandle,
    IN void* BaseAddress
);



MTNATIVE_API MTSTATUS
MtCreateProcess(
    IN const MT_CREATE_PROCESS_PARAMETERS* Parameters,
    OUT PMT_PROCESS_INFORMATION ProcessInformation
);

MTNATIVE_API MTSTATUS
MtPrintConsole(
    IN uint32_t Color,
    IN const char* String
);

#ifdef __cplusplus
}
#endif

#endif /* MATANELOS_SHARED_MTNATIVE_H */
