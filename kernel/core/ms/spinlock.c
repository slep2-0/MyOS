/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      Spinlock Types and Function Declarations. (MS)
 */
#ifndef X86_SPINLOCK_H
#define X86_SPINLOCK_H

#include "../../includes/ms.h"
#include "../../includes/me.h"

void 
MsAcquireSpinlock (
	IN	PSPINLOCK lock,
	IN	PIRQL OldIrql
) 

/*++

	Routine description : Acquires a spinlock, raises IRQL to DISPATCH_LEVEL.

	Arguments:

		[IN]    Pointer to SPINLOCK object.
		[IN]	Pointer to Old IRQL variable.

	Return Values:

		None.

--*/

{
	if (!lock) return;
	// spin until we grab the lock.
	MeRaiseIrql(DISPATCH_LEVEL, OldIrql);
	while (__sync_lock_test_and_set(&lock->locked, 1)) {
		__asm__ volatile("pause" ::: "memory"); /* x86 pause — CPU relax hint */
	}
	// Memory barrier to prevent instruction reordering
	__asm__ volatile("" ::: "memory");
}

void 
MsReleaseSpinlock (
	IN	PSPINLOCK lock, 
	IN	IRQL OldIrql
) 

/*++

	Routine description : Releases a spinlock, restores previous IRQL.

	Arguments:

		[IN]    Pointer to SPINLOCK object.
		[IN]    Old IRQL given by MsAcquireSpinlock

	Return Values:

		None.

--*/

{
	if (!lock) return;
	// Memory barrier before release
	__asm__ volatile("" ::: "memory");
	__sync_lock_release(&lock->locked);
	MeLowerIrql(OldIrql);
}

void
MsAcquireSpinlockAtDpcLevel(
	IN PSPINLOCK Lock
)

{
	// Make sure we are at DPC level or above
	if (MeGetCurrentIrql() < DISPATCH_LEVEL) {
		// Bugcheck.
		MeBugCheckEx(
			IRQL_NOT_GREATER_OR_EQUAL,
			(void*)Lock,
			(void*)MeGetCurrentIrql(),
			NULL,
			NULL
		);
	}
	
	// Acquire the spinlock.
	while (__sync_lock_test_and_set(&Lock->locked, 1)) {
		__asm__ volatile("pause" ::: "memory"); /* x86 pause — CPU relax hint */
	}
	// Memory barrier to prevent instruction reordering
	__asm__ volatile("" ::: "memory");
}

void
MsReleaseSpinlockFromDpcLevel(
	IN PSPINLOCK Lock
)

{
	// Make sure we are at DPC level or above
	if (MeGetCurrentIrql() < DISPATCH_LEVEL) {
		// Bugcheck.
		MeBugCheckEx(
			IRQL_NOT_GREATER_OR_EQUAL,
			(void*)Lock,
			(void*)MeGetCurrentIrql(),
			NULL,
			NULL
		);
	}

	// Release the spinlock.
	__asm__ volatile("" ::: "memory");
	__sync_lock_release(&Lock->locked);
}

#endif