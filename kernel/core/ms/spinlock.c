/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      Spinlock Types and Function Declarations. (MS)
 */
#ifndef X86_SPINLOCK_H
#define X86_SPINLOCK_H

#include "../../includes/ms.h" // Contains ms.h as well
#include "../../trace.h"

extern void gop_printf(uint32_t color, const char* fmt, ...);

/// <summary>
/// Use before acquiring a spinlock
/// </summary>
/// <param name="lock">SPINLOCK Object pointer.</param>
static void spinlock_init(SPINLOCK* lock) {
	if (!lock) return;
	lock->locked = 0;
}

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
	tracelast_func("MtAcquireSpinlock");
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
	tracelast_func("MtReleaseSpinlock");
	if (!lock) return;
	// Memory barrier before release
	__asm__ volatile("" ::: "memory");
	__sync_lock_release(&lock->locked);
	MeLowerIrql(OldIrql);
}

#endif