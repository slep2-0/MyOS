#pragma once

// never thought id get here.

// First we must define basic types.
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Then kernel types
#include "mtstatus.h"
#include "accessrights.h"

// Basic definitions.
typedef int32_t HANDLE, * PHANDLE;
typedef uint32_t ACCESS_MASK;
#define IN // Takes REQUIRED INPUT
#define OUT // Supplies REQUIRED OUTPUT
#define _In_Opt // Takes OPTIONAL INPUT if given.
#define _Out_Opt // OPTIONALLY Supplies OUTPUT if given.
#define MtCurrentProcess() -1
#define MtCurrentThread() -2

typedef enum _USER_ALLOCATION_TYPE {
    PAGE_EXECUTE_READ = 0x10, // PRESENT
    PAGE_EXECUTE_READWRITE = 0x20, // PRESENT | RW
    PAGE_READWRITE = 0x30, // PRESENT | RW | NX
    PAGE_READONLY = 0x40 // PRESENT | NX
} USER_ALLOCATION_TYPE;

// System calls. (TODO mtdll.mtdll, funny name)
MTSTATUS
MtAllocateVirtualMemory(
    IN HANDLE Process,
    _In_Opt _Out_Opt void** BaseAddress,
    IN size_t NumberOfBytes,
    IN uint8_t AllocationType
);
