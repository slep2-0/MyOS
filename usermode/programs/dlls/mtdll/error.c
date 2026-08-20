#include "includes/errorhandlingapi.h"
#include "includes/mtdll.h"

MTDLL_API
ERROR_CODE GetLastError(
	void
)

/*++

    Routine description:

        Retrieves the last error value stored in the current thread's TEB.

    Arguments:

        None.

    Return Values:

        The current thread's ERROR_CODE value.

--*/

{
	// Return last error value from the TEB.
	return MtCurrentTeb()->LastErrorValue;
}

MTDLL_API
void SetLastError(
	ERROR_CODE dwErrorCode
)

/*++

    Routine description:

        Stores an error value in the current thread's TEB.

    Arguments:

        [IN] dwErrorCode - The error value to store.

    Return Values:

        None.

--*/

{
	MtCurrentTeb()->LastErrorValue = dwErrorCode;
}

// Private MTDLL API

MTSTATUS GetLastStatus(
	void
)

/*++

    Routine description:

        Retrieves the last native status stored in the current thread's TEB.

    Arguments:

        None.

    Return Values:

        The current thread's MTSTATUS value.

--*/

{
	return MtCurrentTeb()->LastStatusValue;
}

void SetLastStatus(
	MTSTATUS dwStatusCode
)

/*++

    Routine description:

        Stores a native status in the current thread's TEB.

    Arguments:

        [IN] dwStatusCode - The native status to store.

    Return Values:

        None.

--*/

{
	MtCurrentTeb()->LastStatusValue = dwStatusCode;
}
