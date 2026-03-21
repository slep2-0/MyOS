#include "includes/errorhandlingapi.h"
#include "includes/mtdll.h"

ERROR_CODE GetLastError(
	void
)

{
	// Return last error value from the TEB.
	return MtCurrentTeb()->LastErrorValue;
}

void SetLastError(
	ERROR_CODE dwErrorCode
)

{
	MtCurrentTeb()->LastErrorValue = dwErrorCode;
}

// Private MTDLL API

MTSTATUS GetLastStatus(
	void
)

{
	return MtCurrentTeb()->LastStatusValue;
}

void SetLastStatus(
	MTSTATUS dwStatusCode
)

{
	MtCurrentTeb()->LastStatusValue = dwStatusCode;
}