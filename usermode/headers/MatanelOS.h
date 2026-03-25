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

// Include other MatanelOS user mode headers. (defaults)
#include "errorhandlingapi.h"
#include "accessrights.h"

// Internal ones have to be added manually by user, like in Windows.

typedef struct {
    uint64_t module_name_rva;
    uint64_t function_name_rva;
    uint64_t func_ptr_addr_rva;
} MT_IMPORT;

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

extern HANDLE
(*CreateThread)(
    IN THREAD_START_ROUTINE StartRoutine,
    IN void* ThreadParameter
);

extern HANDLE
(*CreateRemoteThread)(
    IN HANDLE ProcessHandle,
    IN THREAD_START_ROUTINE StartRoutine,
    IN void* ThreadParameter
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
    IN USER_PROTECTION_TYPE AllocationType
);

extern void* (*VirtualAllocEx)(
    IN HANDLE ProcessHandle,
    _In_Opt _Out_Opt void** BaseAddress,
    IN size_t AllocationSize,
    IN USER_PROTECTION_TYPE AllocationType
);

extern bool (*VirtualQuery)(
    IN void* BaseAddress,
    OUT PMEMORY_BASIC_INFORMATION MemoryInformation
);

extern bool (*VirtualQueryEx)(
    IN HANDLE ProcessHandle,
    IN void* BaseAddress,
    OUT PMEMORY_BASIC_INFORMATION MemoryInformation
);


extern bool (*VirtualProtect)(
    IN void* BaseAddress,
    IN size_t RegionSize,
    IN USER_PROTECTION_TYPE NewProtection,
    OUT USER_PROTECTION_TYPE* OldProtection
);

extern bool (*VirtualProtectEx)(
    IN HANDLE ProcessHandle,
    IN void* BaseAddress,
    IN size_t RegionSize,
    IN USER_PROTECTION_TYPE NewProtection,
    OUT USER_PROTECTION_TYPE* OldProtection
);

extern bool (*VirtualFree)(
    IN void* BaseAddress,
    IN size_t NumberOfBytes,
    IN FREE_TYPE FreeType
);

extern bool (*VirtualFreeEx)(
    IN HANDLE ProcessHandle,
    IN void* BaseAddress,
    IN size_t NumberOfBytes,
    IN FREE_TYPE FreeType
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

extern void
(*Sleep)(
    IN uint32_t Milliseconds
);

extern uint32_t
(*WaitForSingleObject)(
    IN HANDLE ObjectHandle,
    IN uint32_t Milliseconds
);