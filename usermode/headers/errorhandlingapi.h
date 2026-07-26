#pragma once

/*++

Module Name:

	errorhandlingapi.h

Purpose:

	This header contains the prototypes, structures, enumerators, and functions required for error handling in user mode.

Author:

	slep (Matanel) 2025.

Revision History:

--*/

#include "../../shared/include/errorcodes.h"
#include "mtapi.h"


// Error handling API from MTDLL.

MTDLL_API ERROR_CODE GetLastError(
	void
);

MTDLL_API void SetLastError(
	ERROR_CODE dwErrorCode
);

