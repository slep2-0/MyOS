#include "includes/synchapi.h"
#include "includes/errorhandlingapi.h"

static uint32_t
MtdllWaitStatusToResult(
	IN MTSTATUS Status
)
{
	SetLastStatus(Status);

	switch (Status) {
	case MT_SUCCESS:
		return WAIT_OBJECT_0;
	case MT_TIMEOUT:
		return WAIT_TIMEOUT;
	case MT_MUTEX_ABANDONED:
		return WAIT_ABANDONED_0;
	default:
		SetLastError(MtStatusToLastError(Status));
		return WAIT_FAILED;
	}
}

static bool
MtdllBooleanResult(
	IN MTSTATUS Status
)
{
	SetLastStatus(Status);
	SetLastError(MtStatusToLastError(Status));
	return MT_SUCCEEDED(Status);
}

void
Sleep(
	IN uint32_t Milliseconds
)

{
	MTSTATUS Status = MtDelayExecution(false, Milliseconds);
	SetLastStatus(Status);
	SetLastError(MtStatusToLastError(Status));
}

uint32_t
WaitForSingleObject(
	IN HANDLE ObjectHandle,
	IN uint64_t Milliseconds
)

{
	MTSTATUS Status = MtWaitForSingleObject(ObjectHandle, Milliseconds, false);
	return MtdllWaitStatusToResult(Status);
}

uint32_t
WaitForSingleObjectEx(
	IN HANDLE ObjectHandle,
	IN uint64_t Milliseconds,
	IN bool Alertable
)

{
	MTSTATUS Status = MtWaitForSingleObject(ObjectHandle, Milliseconds, Alertable);
	return MtdllWaitStatusToResult(Status);
}

HANDLE
CreateEvent(
	IN EVENT_TYPE EventType,
	IN bool InitialState,
	_In_Opt const char* Name
)
{
	HANDLE EventHandle = MT_INVALID_HANDLE;
	MTSTATUS Status = MtCreateEvent(
		&EventHandle,
		MT_EVENT_ALL_ACCESS,
		EventType,
		InitialState,
		Name
	);
	SetLastStatus(Status);
	SetLastError(MtStatusToLastError(Status));
	return MT_SUCCEEDED(Status) ? EventHandle : MT_INVALID_HANDLE;
}

bool
QueryEvent(
	IN HANDLE EventHandle,
	OUT bool* SignalState
)
{
	return MtdllBooleanResult(MtQueryEvent(EventHandle, SignalState));
}

bool
SetEvent(
	IN HANDLE EventHandle
)
{
	return MtdllBooleanResult(MtSetEvent(EventHandle, NULL));
}

bool
ResetEvent(
	IN HANDLE EventHandle
)
{
	return MtdllBooleanResult(MtResetEvent(EventHandle, NULL));
}

HANDLE
CreateMutex(
	IN bool InitialOwner,
	_In_Opt const char* Name
)
{
	HANDLE MutexHandle = MT_INVALID_HANDLE;
	MTSTATUS Status = MtCreateMutex(
		&MutexHandle,
		MT_MUTEX_ALL_ACCESS,
		InitialOwner,
		Name
	);
	SetLastStatus(Status);
	SetLastError(MtStatusToLastError(Status));
	return MT_SUCCEEDED(Status) ? MutexHandle : MT_INVALID_HANDLE;
}

bool
QueryMutex(
	IN HANDLE MutexHandle,
	OUT PMUTEX_BASIC_INFORMATION Information
)
{
	return MtdllBooleanResult(MtQueryMutex(MutexHandle, Information));
}

bool
ReleaseMutex(
	IN HANDLE MutexHandle
)
{
	return MtdllBooleanResult(MtReleaseMutex(MutexHandle, NULL));
}

HANDLE
CreateSemaphore(
	IN int32_t InitialCount,
	IN int32_t MaximumCount,
	_In_Opt const char* Name
)
{
	HANDLE SemaphoreHandle = MT_INVALID_HANDLE;
	MTSTATUS Status = MtCreateSemaphore(
		&SemaphoreHandle,
		MT_SEMAPHORE_ALL_ACCESS,
		InitialCount,
		MaximumCount,
		Name
	);
	SetLastStatus(Status);
	SetLastError(MtStatusToLastError(Status));
	return MT_SUCCEEDED(Status) ? SemaphoreHandle : MT_INVALID_HANDLE;
}

bool
QuerySemaphore(
	IN HANDLE SemaphoreHandle,
	OUT PSEMAPHORE_BASIC_INFORMATION Information
)
{
	return MtdllBooleanResult(MtQuerySemaphore(SemaphoreHandle, Information));
}

bool
ReleaseSemaphore(
	IN HANDLE SemaphoreHandle,
	IN int32_t ReleaseCount,
	_Out_Opt int32_t* PreviousCount
)
{
	return MtdllBooleanResult(
		MtReleaseSemaphore(SemaphoreHandle, ReleaseCount, PreviousCount)
	);
}
