#pragma once

#include "mtdll.h"
#include <stdbool.h>

MTDLL_API bool
TerminateThread(
	IN HANDLE ThreadHandle,
	IN uint32_t ExitStatus
);

MTDLL_API HANDLE
CreateThread(
	IN THREAD_START_ROUTINE StartRoutine,
	IN void* ThreadParameter
);

MTDLL_API HANDLE
CreateRemoteThread(
	IN HANDLE ProcessHandle,
	IN THREAD_START_ROUTINE StartRoutine,
	IN void* ThreadParameter
);
