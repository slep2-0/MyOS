#include "includes/synchapi.h"
#include "includes/errorhandlingapi.h"

static uint32_t
MtdllWaitStatusToResult(
	IN MTSTATUS Status
)

/*++

    Routine description:

        Converts a native wait status to the public WAIT_* result values.

    Arguments:

        [IN] Status - The native status returned by the wait service.

    Return Values:

        WAIT_OBJECT_0 for success, WAIT_TIMEOUT for a timeout,
        WAIT_ABANDONED_0 for an abandoned mutex, or WAIT_FAILED for another
        failure.

--*/
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

/*++

    Routine description:

        Converts a native status to a Boolean result and updates the current
        thread's last-status and last-error values.

    Arguments:

        [IN] Status - The native status to translate.

    Return Values:

        true when Status represents success, or false otherwise.

--*/
{
	SetLastStatus(Status);
	SetLastError(MtStatusToLastError(Status));
	return MT_SUCCEEDED(Status);
}

MTDLL_API
void
Sleep(
	IN uint32_t Milliseconds
)

/*++

    Routine description:

        Delays the current thread for the requested number of milliseconds.

    Arguments:

        [IN] Milliseconds - The delay interval in milliseconds.

    Return Values:

        None. Failure is reported through the current thread's last-status and
        last-error values.

--*/

{
	MTSTATUS Status = MtDelayExecution(false, Milliseconds);
	SetLastStatus(Status);
	SetLastError(MtStatusToLastError(Status));
}

MTDLL_API
uint32_t
WaitForSingleObject(
	IN HANDLE ObjectHandle,
	IN uint64_t Milliseconds
)

/*++

    Routine description:

        Waits for a dispatcher object to become signaled.

    Arguments:

        [IN] ObjectHandle - The handle of the object to wait for.
        [IN] Milliseconds - The maximum wait interval in milliseconds.

    Return Values:

        WAIT_OBJECT_0 when the object is satisfied, WAIT_TIMEOUT when the
        interval expires, WAIT_ABANDONED_0 for an abandoned mutex, or
        WAIT_FAILED when the wait fails.

--*/

{
	MTSTATUS Status = MtWaitForSingleObject(ObjectHandle, Milliseconds, false);
	return MtdllWaitStatusToResult(Status);
}

MTDLL_API
uint32_t
WaitForSingleObjectEx(
	IN HANDLE ObjectHandle,
	IN uint64_t Milliseconds,
	IN bool Alertable
)

/*++

    Routine description:

        Waits for a dispatcher object with the requested alertability.

    Arguments:

        [IN] ObjectHandle - The handle of the object to wait for.
        [IN] Milliseconds - The maximum wait interval in milliseconds.
        [IN] Alertable - Whether an alertable wait is requested.

    Return Values:

        WAIT_OBJECT_0 when the object is satisfied, WAIT_TIMEOUT when the
        interval expires, WAIT_ABANDONED_0 for an abandoned mutex, or
        WAIT_FAILED when the wait fails.

--*/

{
	MTSTATUS Status = MtWaitForSingleObject(ObjectHandle, Milliseconds, Alertable);
	return MtdllWaitStatusToResult(Status);
}

MTDLL_API
HANDLE
CreateEvent(
	IN EVENT_TYPE EventType,
	IN bool InitialState,
	_In_Opt const char* Name
)

/*++

    Routine description:

        Creates an event object and returns a handle with event access.

    Arguments:

        [IN] EventType - The event signaling semantics.
        [IN] InitialState - Whether the event starts signaled.
        [IN OPTIONAL] Name - The name assigned to the event, if any.

    Return Values:

        A valid event handle on success, or MT_INVALID_HANDLE on failure.

--*/
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

MTDLL_API
bool
QueryEvent(
	IN HANDLE EventHandle,
	OUT bool* SignalState
)

/*++

    Routine description:

        Retrieves the current signal state of an event object.

    Arguments:

        [IN] EventHandle - The event handle to query.
        [OUT] SignalState - Receives true when the event is signaled.

    Return Values:

        true when the query succeeds, or false on failure.

--*/
{
	return MtdllBooleanResult(MtQueryEvent(EventHandle, SignalState));
}

MTDLL_API
bool
SetEvent(
	IN HANDLE EventHandle
)

/*++

    Routine description:

        Signals an event and wakes eligible waiters.

    Arguments:

        [IN] EventHandle - The event handle to signal.

    Return Values:

        true when the event is signaled, or false on failure.

--*/
{
	return MtdllBooleanResult(MtSetEvent(EventHandle, NULL));
}

MTDLL_API
bool
ResetEvent(
	IN HANDLE EventHandle
)

/*++

    Routine description:

        Resets an event to its nonsignaled state.

    Arguments:

        [IN] EventHandle - The event handle to reset.

    Return Values:

        true when the event is reset, or false on failure.

--*/
{
	return MtdllBooleanResult(MtResetEvent(EventHandle, NULL));
}

MTDLL_API
HANDLE
CreateMutex(
	IN bool InitialOwner,
	_In_Opt const char* Name
)

/*++

    Routine description:

        Creates a mutex object and optionally assigns it to the current
        thread.

    Arguments:

        [IN] InitialOwner - Whether the current thread initially owns the
        mutex.
        [IN OPTIONAL] Name - The name assigned to the mutex, if any.

    Return Values:

        A valid mutex handle on success, or MT_INVALID_HANDLE on failure.

--*/
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

MTDLL_API
bool
QueryMutex(
	IN HANDLE MutexHandle,
	OUT PMUTEX_BASIC_INFORMATION Information
)

/*++

    Routine description:

        Retrieves basic state for a mutex object.

    Arguments:

        [IN] MutexHandle - The mutex handle to query.
        [OUT] Information - Receives the mutex information.

    Return Values:

        true when the query succeeds, or false on failure.

--*/
{
	return MtdllBooleanResult(MtQueryMutex(MutexHandle, Information));
}

MTDLL_API
bool
ReleaseMutex(
	IN HANDLE MutexHandle
)

/*++

    Routine description:

        Releases ownership of a mutex held by the current thread.

    Arguments:

        [IN] MutexHandle - The mutex handle to release.

    Return Values:

        true when ownership is released, or false when the current thread does
        not own the mutex or the operation fails.

--*/
{
	return MtdllBooleanResult(MtReleaseMutex(MutexHandle, NULL));
}

MTDLL_API
HANDLE
CreateSemaphore(
	IN int32_t InitialCount,
	IN int32_t MaximumCount,
	_In_Opt const char* Name
)

/*++

    Routine description:

        Creates a semaphore with an initial and maximum count.

    Arguments:

        [IN] InitialCount - The count initially available to satisfy waits.
        [IN] MaximumCount - The greatest count the semaphore can hold.
        [IN OPTIONAL] Name - The name assigned to the semaphore, if any.

    Return Values:

        A valid semaphore handle on success, or MT_INVALID_HANDLE on failure.

--*/
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

MTDLL_API
bool
QuerySemaphore(
	IN HANDLE SemaphoreHandle,
	OUT PSEMAPHORE_BASIC_INFORMATION Information
)

/*++

    Routine description:

        Retrieves basic state for a semaphore object.

    Arguments:

        [IN] SemaphoreHandle - The semaphore handle to query.
        [OUT] Information - Receives the semaphore information.

    Return Values:

        true when the query succeeds, or false on failure.

--*/
{
	return MtdllBooleanResult(MtQuerySemaphore(SemaphoreHandle, Information));
}

MTDLL_API
bool
ReleaseSemaphore(
	IN HANDLE SemaphoreHandle,
	IN int32_t ReleaseCount,
	_Out_Opt int32_t* PreviousCount
)

/*++

    Routine description:

        Increases a semaphore's count and releases eligible waiters.

    Arguments:

        [IN] SemaphoreHandle - The semaphore handle to release.
        [IN] ReleaseCount - The number to add to the semaphore count.
        [OUT OPTIONAL] PreviousCount - Receives the count before adjustment.

    Return Values:

        true when the count is adjusted, or false when the operation fails or
        would exceed the maximum count.

--*/
{
	return MtdllBooleanResult(
		MtReleaseSemaphore(SemaphoreHandle, ReleaseCount, PreviousCount)
	);
}
