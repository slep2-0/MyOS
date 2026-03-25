#pragma once

#include "mtdll.h"
#include <stdbool.h>

void
Sleep(
	IN uint32_t Milliseconds
);

uint32_t
WaitForSingleObject(
	IN HANDLE ObjectHandle,
	IN uint32_t Milliseconds
);