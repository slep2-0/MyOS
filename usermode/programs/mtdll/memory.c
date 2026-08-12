/*++

Module Name:

    memory.c

Purpose:

    This translation unit contains the standard library functions involving memory allocation and deallocation.

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "includes/mtdll.h"
#include "includes/exports.h"
#include "includes/errorhandlingapi.h"

MTDLL_API
void*
memcpy(
    void* RESTRICT Destination,
    const void* RESTRICT Source,
    size_t Size
)

/*++

    Routine description:

        Copies a byte range between buffers.

    Arguments:

        [OUT] Destination - The destination buffer.
        [IN] Source - The source buffer.
        [IN] Size - The number of bytes to copy.

    Return Values:

        Destination.

    Notes:

        The source and destination ranges must be valid and non-overlapping.

--*/

{
    void* Result = Destination;

    // Use rep movsb because its fast af on modern processors
    __asm__ volatile (
        "rep movsb"
        : "+D"(Destination),
        "+S"(Source),
        "+c"(Size)
        :
        : "memory"
        );

    return Result;
}

MTDLL_API
void*
memset(
    void* Destination,
    int Value,
    size_t Size
)

/*++

    Routine description:

        Fills a buffer with one byte value.

    Arguments:

        [OUT] Destination - The buffer to fill.
        [IN] Value - The byte value written to the buffer.
        [IN] Size - The number of bytes to write.

    Return Values:

        Destination.

--*/

{
    unsigned char* Bytes = (unsigned char*)Destination;

    while (Size--) {
        *Bytes++ = (unsigned char)Value;
    }

    return Destination;
}

MTDLL_API
void*
VirtualAlloc(
    _In_Opt _Out_Opt void** BaseAddress,
    IN size_t AllocationSize,
    IN USER_PROTECTION_TYPE AllocationType
)

/*++

    Routine description:

        Allocates memory.

    Arguments:

        [IN OPTIONAL | OUT OPTIONAL] [PTR_TO_PTR]   void** BaseAddress - The base address to allocate memory starting from if supplied. If NULL, a free gap is chosen and used by NumberOfBytes, and *BaseAddress is set to the found start of gap.
        [IN]    size_t NumberOfBytes - The amount in virtual memory to allocate.
        [IN]    uint8_t AllocationType - USER_PROTECTION_TYPE Enum specifying which type of PTE flags the allocation should have. (executable, writable, none)

    Return Values:

        Base virtual address to allocated memory, or NULL on failure.

--*/

{
    // Call Ex version with current process handle.
    return VirtualAllocEx(MtCurrentProcess(), BaseAddress, AllocationSize, AllocationType);
}

MTDLL_API
void* VirtualAllocEx(
    IN HANDLE ProcessHandle,
    _In_Opt _Out_Opt void** BaseAddress,
    IN size_t AllocationSize,
    IN USER_PROTECTION_TYPE AllocationType
)

/*++

    Routine description:

        Allocates memory in a remote process.

    Arguments:

        [IN]    HANDLE ProcessHandle - The process handle to allocate memory for (special handles allowed).
        [IN OPTIONAL | OUT OPTIONAL] [PTR_TO_PTR]   void** BaseAddress - The base address to allocate memory starting from if supplied. If NULL, a free gap is chosen and used by NumberOfBytes, and *BaseAddress is set to the found start of gap.
        [IN]    size_t NumberOfBytes - The amount in virtual memory to allocate.
        [IN]    uint8_t AllocationType - USER_PROTECTION_TYPE Enum specifying which type of PTE flags the allocation should have. (executable, writable, none)

    Return Values:

        Base virtual address to allocated memory, or NULL on failure.

--*/


{
    void* Address = NULL;
    MTSTATUS Status;

    if (!BaseAddress) {
        // Caller did not supply a BaseAddress pointer
        void* TmpAddr = NULL;
        Status = MtAllocateVirtualMemory(ProcessHandle, &TmpAddr, AllocationSize, AllocationType);
        Address = TmpAddr;  // safe
    }
    else {
        // Caller provided a pointer to a pointer
        Status = MtAllocateVirtualMemory(ProcessHandle, BaseAddress, AllocationSize, AllocationType);
        if (MT_SUCCEEDED(Status)) {
            Address = *BaseAddress;
        }
    }

    SetLastStatus(Status);
    SetLastError(MtStatusToLastError(Status));
    if (MT_SUCCEEDED(Status)) {
        return Address;
    }

    return NULL;
}

MTDLL_API
bool
VirtualQuery(
    IN void* BaseAddress,
    OUT PMEMORY_BASIC_INFORMATION MemoryInformation
)

/*++

    Routine description:

        Queries the virtual memory region containing an address.

    Arguments:

        [IN] BaseAddress - An address within the region to query.
        [OUT] MemoryInformation - Receives the region information.

    Return Values:

        true when the query succeeds, or false when the native query fails.

--*/

{
    return VirtualQueryEx(MtCurrentProcess(), BaseAddress, MemoryInformation);
}

MTDLL_API
bool
VirtualQueryEx(
    IN HANDLE ProcessHandle,
    IN void* BaseAddress,
    OUT PMEMORY_BASIC_INFORMATION MemoryInformation
)

/*++

    Routine description:

        Queries a virtual memory region in a target process.

    Arguments:

        [IN] ProcessHandle - The process whose address space is queried.
        [IN] BaseAddress - An address within the region to query.
        [OUT] MemoryInformation - Receives the region information.

    Return Values:

        true when the query succeeds, or false when the native query fails.

--*/

{
    // Call kernel.
    MTSTATUS Status = MtQueryVirtualMemory(ProcessHandle, BaseAddress, MemoryInformation);
    SetLastStatus(Status);
    SetLastError(MtStatusToLastError(Status));

    return MT_SUCCEEDED(Status);
}

MTDLL_API
bool
VirtualProtect(
    IN void* BaseAddress,
    IN size_t RegionSize,
    IN USER_PROTECTION_TYPE NewProtection,
    OUT USER_PROTECTION_TYPE* OldProtection
)

/*++

    Routine description:

        Changes protection on a virtual memory region.

    Arguments:

        [IN] BaseAddress - The first address in the region to protect.
        [IN] RegionSize - The size of the region in bytes.
        [IN] NewProtection - The protection to apply.
        [OUT] OldProtection - Receives the previous protection.

    Return Values:

        true when protection changes successfully, or false on failure.

--*/

{
    return VirtualProtectEx(MtCurrentProcess(), BaseAddress, RegionSize, NewProtection, OldProtection);
}

MTDLL_API
bool
VirtualProtectEx(
    IN HANDLE ProcessHandle,
    IN void* BaseAddress,
    IN size_t RegionSize,
    IN USER_PROTECTION_TYPE NewProtection,
    OUT USER_PROTECTION_TYPE* OldProtection
)

/*++

    Routine description:

        Changes protection on a target process memory region.

    Arguments:

        [IN] ProcessHandle - The process whose address space is modified.
        [IN] BaseAddress - The first address in the region to protect.
        [IN] RegionSize - The size of the region in bytes.
        [IN] NewProtection - The protection to apply.
        [OUT] OldProtection - Receives the previous protection.

    Return Values:

        true when protection changes successfully, or false on failure.

--*/

{
    // Call kernel.
    void** BaseAddressPtr = &BaseAddress;
    size_t* RegionSizePtr = &RegionSize;
    MTSTATUS Status = MtProtectVirtualMemory(ProcessHandle, BaseAddressPtr, RegionSizePtr, NewProtection, OldProtection); 
    SetLastStatus(Status);
    SetLastError(MtStatusToLastError(Status));

    return MT_SUCCEEDED(Status);
}

MTDLL_API
bool
VirtualFree(
    IN void* BaseAddress,
    IN size_t NumberOfBytes,
    IN FREE_TYPE FreeType
)

/*++

    Routine description:

        Releases virtual memory in the current process.

    Arguments:

        [IN] BaseAddress - The base address of the allocation to release.
        [IN] NumberOfBytes - The requested size used by the release operation.
        [IN] FreeType - The release operation to perform.

    Return Values:

        true when the memory is released, or false on failure.

--*/

{
    return VirtualFreeEx(MtCurrentProcess(), BaseAddress, NumberOfBytes, FreeType);
}

MTDLL_API
bool
VirtualFreeEx(
    IN HANDLE ProcessHandle,
    IN void* BaseAddress,
    IN size_t NumberOfBytes,
    IN FREE_TYPE FreeType
)

/*++

    Routine description:

        Releases virtual memory in a target process.

    Arguments:

        [IN] ProcessHandle - The process whose address space is modified.
        [IN] BaseAddress - The base address of the allocation to release.
        [IN] NumberOfBytes - The requested size used by the release operation.
        [IN] FreeType - The release operation to perform.

    Return Values:

        true when the memory is released, or false on failure.

--*/

{
    // Call kernel.
    void** BaseAddressTemp = &BaseAddress;
    size_t* NumberOfBytesTemp = &NumberOfBytes;

    MTSTATUS Status = MtFreeVirtualMemory(ProcessHandle, BaseAddressTemp, NumberOfBytesTemp, FreeType);
    SetLastStatus(Status);
    SetLastError(MtStatusToLastError(Status));

    return MT_SUCCEEDED(Status);
}
