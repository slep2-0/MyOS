#include "includes/synchapi.h"
#include "includes/errorhandlingapi.h"

void
Sleep(
	IN uint32_t Milliseconds
)

{
	MTSTATUS Status = MtSleep(Milliseconds);
	SetLastStatus(Status);
	SetLastError(MtStatusToLastError(Status));
}

uint32_t
WaitForSingleObject(
	IN HANDLE ObjectHandle,
	IN uint32_t Milliseconds
)

{
	MTSTATUS Status = MtWaitForSingleObject(ObjectHandle, Milliseconds);
	SetLastError(MtStatusToLastError(Status));
	SetLastStatus(Status);

	return Status;
}