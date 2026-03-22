#pragma once

#include "mtdll.h"
#include <stdbool.h>

bool
TerminateThread(
	IN HANDLE ThreadHandle,
	IN uint32_t ExitStatus
);

HANDLE
CreateThread(
	IN THREAD_START_ROUTINE StartRoutine,
	IN void* ThreadParameter
);

HANDLE
CreateRemoteThread(
	IN HANDLE ProcessHandle,
	IN THREAD_START_ROUTINE StartRoutine,
	IN void* ThreadParameter
);