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
    // Call kernel.
    if (!BaseAddress) {
        void* TmpAddr = NULL;
        Status = MtAllocateVirtualMemory(ProcessHandle, &TmpAddr, AllocationSize, AllocationType);
        Address = TmpAddr;
    }
    else {
        Status = MtAllocateVirtualMemory(ProcessHandle, BaseAddress, AllocationSize, AllocationType);
    }

    SetLastError(MtStatusToLastError(Status));
    if (MT_SUCCEEDED(Status)) {
        return Address;
    }

    return NULL;
}

bool
VirtualQuery(
    IN void* BaseAddress,
    OUT PMEMORY_BASIC_INFORMATION MemoryInformation
)

{
    return VirtualQueryEx(MtCurrentProcess(), BaseAddress, MemoryInformation);
}

bool
VirtualQueryEx(
    IN HANDLE ProcessHandle,
    IN void* BaseAddress,
    OUT PMEMORY_BASIC_INFORMATION MemoryInformation
)

{
    // Call kernel.
    MTSTATUS Status = MtQueryVirtualMemory(ProcessHandle, BaseAddress, MemoryInformation);
    SetLastError(MtStatusToLastError(Status));

    return MT_SUCCEEDED(Status);
}

bool
VirtualProtect(
    IN void* BaseAddress,
    IN size_t RegionSize,
    IN USER_PROTECTION_TYPE NewProtection,
    OUT USER_PROTECTION_TYPE* OldProtection
)

{
    return VirtualProtectEx(MtCurrentProcess(), BaseAddress, RegionSize, NewProtection, OldProtection);
}

bool
VirtualProtectEx(
    IN HANDLE ProcessHandle,
    IN void* BaseAddress,
    IN size_t RegionSize,
    IN USER_PROTECTION_TYPE NewProtection,
    OUT USER_PROTECTION_TYPE* OldProtection
)

{
    // Call kernel.
    void** BaseAddressPtr = &BaseAddress;
    size_t* RegionSizePtr = &RegionSize;
    MTSTATUS Status = MtProtectVirtualMemory(ProcessHandle, BaseAddressPtr, RegionSizePtr, NewProtection, OldProtection); 
    SetLastError(MtStatusToLastError(Status));

    return MT_SUCCEEDED(Status);
}

bool
VirtualFree(
    IN void* BaseAddress,
    IN size_t NumberOfBytes,
    IN FREE_TYPE FreeType
)

{
    return VirtualFreeEx(MtCurrentProcess(), BaseAddress, NumberOfBytes, FreeType);
}

bool
VirtualFreeEx(
    IN HANDLE ProcessHandle,
    IN void* BaseAddress,
    IN size_t NumberOfBytes,
    IN FREE_TYPE FreeType
)

{
    // Call kernel.
    void** BaseAddressTemp = &BaseAddress;
    size_t* NumberOfBytesTemp = &NumberOfBytes;

    MTSTATUS Status = MtFreeVirtualMemory(ProcessHandle, BaseAddressTemp, NumberOfBytesTemp, FreeType);
    SetLastError(MtStatusToLastError(Status));

    return MT_SUCCEEDED(Status);
}