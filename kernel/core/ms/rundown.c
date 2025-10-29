#include "../../includes/me.h"

#define TEARDOWN_ACTIVE (1ULL << 63)
#define REFERENCE_COUNT (0x7FFFFFFFFFFFFFFF)

bool 
MsAcquireRundownProtection (
	IN	PRUNDOWN_REF rundown
) 

/*++

	Routine description : Safely acquires rundown protection on a shared resource to prevent it from being deleted or "rundown" while in use.

	Arguments:

		Pointer to RUNDOWN_REF Object.

	Return Values:

		True - The rundown protection acquisition has succeeded, the object is safe from memory deletion.
		False - Teardown has started on the object, handle gracefully.

--*/

{
	uint64_t old_count, new_count;
	do {
		old_count = rundown->Count;
		if (old_count & TEARDOWN_ACTIVE) {
			// Teardown has started, refuse to acquire.
			return false;
		}
		new_count = old_count + 1;
	} while (!InterlockedCompareExchangeU64(&rundown->Count, new_count, old_count));
	return true;
}

void 
MsReleaseRundownProtection (
	IN	PRUNDOWN_REF rundown
) 

/*++

	Routine description : Releases rundown protection from the object.

	Arguments:

		Pointer to RUNDOWN_REF Object.

	Return Values:

		None.

--*/

{
	InterlockedDecrementU64(&rundown->Count);
}

// Wait for rundown (teardown)
void MsWaitForRundownProtectionRelease (
	IN	PRUNDOWN_REF rundown
) 

/*++

	Routine description : Waits until all rundown protections have been released from the object, then starts Teardown.
	Use this when you want to gurantee an object will not be used after free.

	Arguments:

		Pointer to RUNDOWN_REF Object.

	Return Values:

		None.

--*/

{
	uint64_t old_count;
	do {
		old_count = rundown->Count;
	} while (!InterlockedCompareExchangeU64(&rundown->Count, old_count | TEARDOWN_ACTIVE, old_count));

	// Spin until count reaches zero (TODO RUNDOWN REF WAKE SLEEP (use the 0-62 bits for a pointer )
	while ((rundown->Count & REFERENCE_COUNT) != 0) {
		__pause();
	}
}