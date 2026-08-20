#include "../../includes/me.h"
#include "../../intrinsics/atomic.h"

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
	uint64_t expected, desired;
	do {
		expected = InterlockedLoad(&rundown->Count);

		// If teardown has started we refuse.
		if (expected & TEARDOWN_ACTIVE) return false;
		if ((expected & REFERENCE_COUNT) == REFERENCE_COUNT) {
			MeBugCheckEx(
				MEMORY_OVERFLOW_DETECTION,
				rundown,
				(void*)(uintptr_t)expected,
				RETADDR(0),
				NULL
			);
		}

		desired = expected + 1;
	} while (!InterlockedCompareExchangeU64_bool(&rundown->Count, desired, &expected));
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
	uint64_t expected = InterlockedLoad(&rundown->Count);
	for (;;) {
		if ((expected & REFERENCE_COUNT) == 0) {
			MeBugCheckEx(
				MEMORY_OVERFLOW_DETECTION,
				rundown,
				(void*)(uintptr_t)expected,
				RETADDR(0),
				NULL
			);
		}

		uint64_t desired = expected - 1;
		if (InterlockedCompareExchangeU64_bool(&rundown->Count, desired, &expected)) {
			return;
		}
	}
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
	uint64_t expected = InterlockedLoad(&rundown->Count);
	for (;;) {
		uint64_t desired = expected | TEARDOWN_ACTIVE;

		// try to set TEARDOWN_ACTIVE while preserving the refcount bits
		if (InterlockedCompareExchangeU64_bool(&rundown->Count, desired, &expected)) {
			// Success: we hold the TEARDOWN_ACTIVE marker now.
			break;
		}

		if (expected & TEARDOWN_ACTIVE) {
			// Another thread set teardown already
			break;
		}
		// otherwise loop and try again with updated expected
	}

	// Spin until no references remain 
	while ((InterlockedLoad(&rundown->Count) & REFERENCE_COUNT) != 0) {
		__pause();
	}
}
