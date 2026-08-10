#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "annotations.h"
#include "mtapi.h"

typedef struct _MT_HEAP* MT_HEAP_HANDLE;

typedef enum _HEAP_CREATE_OPTIONS {
    HEAP_CREATE_NONE = 0x00000000,
    HEAP_NO_SERIALIZE = 0x00000001,
    HEAP_GENERATE_EXCEPTIONS = 0x00000004,
    HEAP_CREATE_ENABLE_EXECUTE = 0x00040000
} HEAP_CREATE_OPTIONS;

typedef enum _HEAP_ALLOCATE_OPTIONS {
    HEAP_ALLOCATE_NO_OPTIONS = 0x00000000,
    HEAP_ALLOCATE_GENERATE_EXCEPTIONS = 0x00000001,
    HEAP_ALLOCATE_NO_SERIALIZE = 0x00000002,
    HEAP_ALLOCATE_ZERO_MEMORY = 0x00000004
} HEAP_ALLOCATE_OPTIONS;

typedef enum _HEAP_FREE_OPTIONS {
    HEAP_FREE_NO_OPTIONS = 0x00000000,
    HEAP_FREE_NO_SERIALIZE = 0x00000001,
} HEAP_FREE_OPTIONS;

MTDLL_API
MT_HEAP_HANDLE
HeapCreate(
    IN HEAP_CREATE_OPTIONS Options,
    IN size_t InitialSize,
    IN size_t MaximumSize
);

MTDLL_API
void*
HeapAlloc(
    IN MT_HEAP_HANDLE Heap,
    IN HEAP_ALLOCATE_OPTIONS Options,
    IN size_t AllocationSize
);

MTDLL_API
bool
HeapFree(
    IN MT_HEAP_HANDLE Heap,
    IN HEAP_FREE_OPTIONS Options,
    IN void* AllocatedMemory
);

MTDLL_API
bool
HeapDestroy(
    IN MT_HEAP_HANDLE HeapHandle
);

MTDLL_API
MT_HEAP_HANDLE
GetProcessHeap(
    void
);
