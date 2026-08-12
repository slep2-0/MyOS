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

typedef enum _HEAP_SIZE_OPTIONS {
    HEAP_SIZE_NO_OPTIONS = 0x00000000,
    HEAP_SIZE_NO_SERIALIZE = 0x00000001,
} HEAP_SIZE_OPTIONS;

typedef enum _HEAP_REALLOCATION_OPTIONS {
    HEAP_REALLOCATE_NO_OPTIONS = 0x00000000,
    HEAP_REALLOCATE_GENERATE_EXCEPTIONS = 0x00000001,
    HEAP_REALLOCATE_NO_SERIALIZE = 0x00000002,
    HEAP_REALLOCATE_ZERO_MEMORY = 0x00000004,
    HEAP_REALLOCATE_IN_PLACE_ONLY = 0x00000008,
} HEAP_REALLOCATION_OPTIONS;

#define MT_HEAP_SIZE_ERROR ((size_t)-1)

/*++

    Routine description:

        Creates a private heap.

    Arguments:

        [IN] Options - Heap creation options.
        [IN] InitialSize - Preferred initial size in bytes.
        [IN] MaximumSize - Maximum heap size, or zero for no limit.

    Return Values:

        A heap handle, or NULL on failure.

--*/
MTDLL_API
MT_HEAP_HANDLE
HeapCreate(
    IN HEAP_CREATE_OPTIONS Options,
    IN size_t InitialSize,
    IN size_t MaximumSize
);

/*++

    Routine description:

        Allocates memory from a heap.

    Arguments:

        [IN] Heap - The heap handle.
        [IN] Options - Allocation options.
        [IN] AllocationSize - The requested size in bytes.

    Return Values:

        The allocated address, or NULL on failure. A requested exception is
        raised instead of returning NULL.

--*/
MTDLL_API
void*
HeapAlloc(
    IN MT_HEAP_HANDLE Heap,
    IN HEAP_ALLOCATE_OPTIONS Options,
    IN size_t AllocationSize
);

/*++

    Routine description:

        Frees a heap allocation.

    Arguments:

        [IN] Heap - The owning heap.
        [IN] Options - Free options.
        [IN] AllocatedMemory - The address returned by HeapAlloc.

    Return Values:

        true on success, or false for an invalid or already freed address.

--*/
MTDLL_API
bool
HeapFree(
    IN MT_HEAP_HANDLE Heap,
    IN HEAP_FREE_OPTIONS Options,
    IN void* AllocatedMemory
);

/*++

    Routine description:

        Destroys a private heap and releases its resources.

    Arguments:

        [IN] HeapHandle - The private heap to destroy.

    Return Values:

        true on complete cleanup, or false on failure.

--*/
MTDLL_API
bool
HeapDestroy(
    IN MT_HEAP_HANDLE HeapHandle
);

/*++

    Routine description:

        Returns the usable size of a heap allocation.

    Arguments:

        [IN] HeapHandle - The owning heap.
        [IN] Options - Size-query options.
        [IN] AllocatedMemory - The allocation to query.

    Return Values:

        The usable size, or MT_HEAP_SIZE_ERROR on failure.

--*/
MTDLL_API
size_t
HeapSize(
    IN MT_HEAP_HANDLE HeapHandle,
    IN HEAP_SIZE_OPTIONS Options,
    IN void* AllocatedMemory
);

/*++

    Routine description:

        Resizes a heap allocation.

    Arguments:

        [IN] HeapHandle - The owning heap.
        [IN] Options - Reallocation options.
        [IN] AllocatedMemory - The allocation to resize.
        [IN] NewReAllocationSize - The requested new size.

    Return Values:

        The resized address, or NULL on failure. A requested exception is
        raised instead of returning NULL.

--*/
MTDLL_API
void*
HeapReAlloc(
    IN MT_HEAP_HANDLE HeapHandle,
    IN HEAP_REALLOCATION_OPTIONS Options,
    IN void* AllocatedMemory,
    IN size_t NewReAllocationSize
);

/*++

    Routine description:

        Acquires a heap's serialization mutex.

    Arguments:

        [IN] Heap - The heap to lock.

    Return Values:

        true when acquired, or false when the heap cannot be locked.

--*/
MTDLL_API
bool
HeapLock(
    IN MT_HEAP_HANDLE Heap
);

/*++

    Routine description:

        Releases a heap's serialization mutex.

    Arguments:

        [IN] Heap - The heap to unlock.

    Return Values:

        true when released, or false when the operation fails.

--*/
MTDLL_API
bool
HeapUnlock(
    IN MT_HEAP_HANDLE Heap
);

/*++

    Routine description:

        Returns the current process default heap.

    Arguments:

        None.

    Return Values:

        The process heap handle, or NULL when it is unavailable.

--*/
MTDLL_API
MT_HEAP_HANDLE
GetProcessHeap(
    void
);
