/*++

Module Name:

    raise.c

Purpose:

    This translation unit contains the implementation of raising status codes in the kernel.

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "../../includes/exception.h"
#include "../../includes/ps.h"
NORETURN
void
ExpRaiseStatus(
    IN MTSTATUS Status
)

/*++

    Routine description:

        Provides the kernel-internal software status-raise boundary. Kernel
        exception search and unwind are not implemented yet, so the current
        routine terminates the system deterministically instead of jumping
        across an arbitrary active C stack.

    Arguments:

        [IN] Status - MTSTATUS Status code that will be visible in the EXCEPTION_RECORD structure.

    Return Values:

        None. This function does not return.

    Notes:

        User-mode exception publication uses ExpPublishUserException and does
        not pass through this routine. Full kernel-mode exception handling is
        tracked as a deferred milestone.

--*/

{
    MeBugCheckEx(
        KMODE_EXCEPTION_NOT_HANDLED,
        (void*)(uintptr_t)Status,
        NULL,
        NULL,
        NULL
    );
}
