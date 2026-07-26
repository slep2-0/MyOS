#pragma once

/*++

Module Name:

	exports.h

Purpose:

	This module contains the functions that will get exported from mtdll.

Author:

	slep (Matanel) 2025.

Revision History:

--*/

#include "mtdll.h"
#include "synchapi.h"
#include <stdbool.h>

// module: string.c
MTDLL_API char* strchr(const char* s, int c);
MTDLL_API char* strncat(char* dest, const char* src, size_t max_len);
MTDLL_API int strncmp(const char* s1, const char* s2, size_t length);
MTDLL_API int strcmp(const char* s1, const char* s2);
MTDLL_API char* strncpy(char* dst, const char* src, size_t n);
MTDLL_API char* strcpy(char* dst, const char* src);
MTDLL_API size_t strlen(const char* str);

// module: thread.c

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

// module: process.c

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

// module: memory.c

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

// module: file.c

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


// module: procldr.c

MTDLL_API void
LdrInitializeProcess(
	IN PPEB InitialPeb,
	IN PTEB InitialTeb,
	IN uint64_t EntryPoint,
	IN PMTDLL_BASIC_TYPES BasicTypes
);

// module: thrdldr.c

MTDLL_API void
LdrInitializeThread(
	IN PTEB Teb,
	IN PPEB Peb,
	IN uint64_t EntryPoint,
	IN uintptr_t ThreadParameter
);
