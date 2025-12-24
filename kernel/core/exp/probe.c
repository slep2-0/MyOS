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
    // Standard assertion to check if alignment meets function requirements. (and CPU)
    assert((Alignment == 1) || (Alignment == 2) || (Alignment == 4) || (Alignment == 8));

    // Check Alignment
    if (((uint64_t)Address & (Alignment - 1)) != 0) {
        return MT_DATATYPE_MISALIGNMENT;
    }

    if (Length != 0) {
        uint64_t Start = (uint64_t)Address;
        uint64_t End = Start + Length; // Integer addition, exact bytes

        // Check Overflow (Wrap around)
        if (End < Start) {
            return MT_ACCESS_VIOLATION;
        }

        // Check against Highest User Address
        // This should not be the MmUserProbeAddress, as the stack lives above that address, and if a user gives something from his stack there, it would resolve in an MT_ACCESS_VIOLATION.
        if (End > (uint64_t)MmHighestUserAddress) {
            return MT_ACCESS_VIOLATION;
        }
    }
    return MT_SUCCESS;
}