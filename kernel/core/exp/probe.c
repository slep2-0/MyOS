/*++

Module Name:

    probe.c

Purpose:

    This translation unit contains the implementation of probing user addresses.

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "../../includes/exception.h"
#include "../../assert.h"

MTSTATUS
ProbeForRead(
    IN const void* Address,
    IN size_t Length,
    IN uint32_t Alignment
)

/*++

    Routine description:

        Checks if the given user address is within the correct bounds and alignment of access.
        The function checks internally after type alignment check if the PreviousMode is KernelMode to return, that means Kernel supplied callers pointers will not be checked.

    Arguments:

        [IN] const void* Address - The user address to check.
        [IN] size_t Length - Length in bytes of user mode buffer.
        [IN] uint32_t Alignment - Required alignment (in bytes) of user mode buffer. (1 for char, 2 for word, 4 for int, 8 for long long)

    Return Values:

        MTSTATUS Code.

        MT_SUCCESS - Address is fine and meets boundaries and length + alignment.
        MT_DATAYPE_MISALIGNMENT - The address given is not aligned properly with the datatype alignment given.
        MT_ACCESS_VIOLATION - The address given is not within boundaries of the user mode virtual address range.

--*/

{
    if (!Address) return MT_ACCESS_VIOLATION;
    if (Alignment != 1 && Alignment != 2 && Alignment != 4 && Alignment != 8) {
        return MT_INVALID_PARAM;
    }

    // Check Alignment
    if (((uint64_t)Address & (Alignment - 1)) != 0) {
        return MT_DATATYPE_MISALIGNMENT;
    }

    // Now kernel mode should pass.
    if (MeGetPreviousMode() == KernelMode) {
        return MT_SUCCESS;
    }

    uint64_t Start = (uint64_t)Address;
    uint64_t Highest = (uint64_t)MmHighestUserAddress;
    if (Start > Highest) {
        return MT_ACCESS_VIOLATION;
    }

    // Validate the inclusive final byte without allowing Start + Length to
    // wrap. Length zero still requires Address itself to be a user address.
    if (Length != 0 && (Length - 1) > (Highest - Start)) {
        return MT_ACCESS_VIOLATION;
    }

    return MT_SUCCESS;
}
