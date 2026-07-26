#ifndef MATANELOS_MTDLL_SYNCHAPI_H
#define MATANELOS_MTDLL_SYNCHAPI_H

#include "mtdll.h"
#include "../../../../shared/include/synchapi.h"

MTDLL_API void
Sleep(
	IN uint32_t Milliseconds
);

MTDLL_API uint32_t
WaitForSingleObject(
	IN HANDLE ObjectHandle,
	IN uint32_t Milliseconds
);


MTDLL_API uint32_t
WaitForSingleObjectEx(
	IN HANDLE ObjectHandle,
	IN uint32_t Milliseconds,
    IN bool Alertable
);

MTDLL_API HANDLE
CreateEvent(
    IN EVENT_TYPE EventType,
    IN bool InitialState,
    _In_Opt const char* Name
);

MTDLL_API bool
QueryEvent(
    IN HANDLE EventHandle,
    OUT bool* SignalState
);

MTDLL_API bool SetEvent(IN HANDLE EventHandle);
MTDLL_API bool ResetEvent(IN HANDLE EventHandle);

MTDLL_API HANDLE
CreateMutex(
    IN bool InitialOwner,
    _In_Opt const char* Name
);

MTDLL_API bool
QueryMutex(
    IN HANDLE MutexHandle,
    OUT PMUTEX_BASIC_INFORMATION Information
);

MTDLL_API bool ReleaseMutex(IN HANDLE MutexHandle);

MTDLL_API HANDLE
CreateSemaphore(
    IN int32_t InitialCount,
    IN int32_t MaximumCount,
    _In_Opt const char* Name
);

MTDLL_API bool
QuerySemaphore(
    IN HANDLE SemaphoreHandle,
    OUT PSEMAPHORE_BASIC_INFORMATION Information
);

MTDLL_API bool
ReleaseSemaphore(
    IN HANDLE SemaphoreHandle,
    IN int32_t ReleaseCount,
    _Out_Opt int32_t* PreviousCount
);

#endif /* MATANELOS_MTDLL_SYNCHAPI_H */
