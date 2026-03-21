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
#include <stdbool.h>

// module: string.c
char* strchr(const char* s, int c);
char* strncat(char* dest, const char* src, size_t max_len);
int strncmp(const char* s1, const char* s2, size_t length);
int strcmp(const char* s1, const char* s2);
char* strncpy(char* dst, const char* src, size_t n);
char* strcpy(char* dst, const char* src);
size_t strlen(const char* str);
char* strncat(char* dest, const char* src, size_t max_len);

// module: thread.c

bool
TerminateThread(
	IN HANDLE ThreadHandle,
	IN uint32_t ExitStatus
);

// module: process.c

HANDLE
OpenProcess(
	IN  ACCESS_MASK DesiredAccess,
	IN  uint32_t ProcessId
);

bool
TerminateProcess(
	IN  HANDLE ProcessHandle,
	IN  uint32_t ExitCode
);

// module: memory.c

void*
VirtualAlloc(
	_In_Opt _Out_Opt void** BaseAddress,
	IN size_t AllocationSize,
	IN USER_PROTECTION_TYPE AllocationType
);

void* 
VirtualAllocEx(
	IN HANDLE ProcessHandle,
	_In_Opt _Out_Opt void** BaseAddress,
	IN size_t AllocationSize,
	IN USER_PROTECTION_TYPE AllocationType
);

bool
VirtualQuery(
	IN void* BaseAddress,
	OUT PMEMORY_BASIC_INFORMATION MemoryInformation
);

bool
VirtualQueryEx(
	IN HANDLE ProcessHandle,
	IN void* BaseAddress,
	OUT PMEMORY_BASIC_INFORMATION MemoryInformation
);

bool
VirtualProtect(
	IN void* BaseAddress,
	IN size_t RegionSize,
	IN USER_PROTECTION_TYPE NewProtection,
	OUT USER_PROTECTION_TYPE* OldProtection
);

bool
VirtualProtectEx(
	IN HANDLE ProcessHandle,
	IN void* BaseAddress,
	IN size_t RegionSize,
	IN USER_PROTECTION_TYPE NewProtection,
	OUT USER_PROTECTION_TYPE* OldProtection
);

bool
VirtualFree(
	IN void* BaseAddress,
	IN size_t NumberOfBytes,
	IN FREE_TYPE FreeType
);

bool
VirtualFreeEx(
	IN HANDLE ProcessHandle,
	IN void* BaseAddress,
	IN size_t NumberOfBytes,
	IN FREE_TYPE FreeType
);

// module: file.c

HANDLE
CreateFile(
	IN  const char* FileName,
	IN  ACCESS_MASK DesiredAccess
);

bool
WriteFile(
	IN HANDLE FileHandle,
	IN uint32_t FileOffset,
	IN void* Buffer,
	IN size_t BufferSize,
	_Out_Opt size_t* BytesWritten
);

bool
ReadFile(
	IN HANDLE FileHandle,
	IN uint32_t FileOffset,
	OUT void* Buffer,
	IN size_t BufferSize,
	_Out_Opt size_t* BytesRead
);


// module: procldr.c

void
LdrInitializeProcess(
	IN PPEB InitialPeb,
	IN PTEB InitialTeb,
	IN uint64_t EntryPoint,
	IN PMTDLL_BASIC_TYPES BasicTypes
);

// module: thrdldr.c

void
LdrInitializeThread(
	IN PTEB Teb,
	IN PPEB Peb,
	IN uint64_t EntryPoint,
	IN uintptr_t ThreadParameter
);