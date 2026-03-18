#include "includes/errorhandlingapi.h"
#include "includes/mtdll.h"

ERROR_CODE GetLastError(
	void
)

{
	// Return last error value from the TEB.
	return NtCurrentTeb()->LastErrorValue;
}

void SetLastError(
	ERROR_CODE dwErrorCode
)

{
	NtCurrentTeb()->LastErrorValue = dwErrorCode;
}