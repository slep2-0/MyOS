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


// Error handling API from MTDLL.

extern ERROR_CODE (*GetLastError)(
	void
);

extern void (*SetLastError)(
	ERROR_CODE dwErrorCode
);

