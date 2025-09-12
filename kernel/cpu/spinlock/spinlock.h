/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      Spinlock Types and Function Declarations.
 */
#ifndef X86_SPINLOCK_H
#define X86_SPINLOCK_H

extern void gop_printf(uint32_t color, const char* fmt, ...);

/// <summary>
/// Use before acquiring a spinlock
/// </summary>
/// <param name="lock">SPINLOCK Object pointer.</param>
static void spinlock_init(SPINLOCK* lock) {
	if (!lock) return;
	lock->locked = 0;
}

/// <summary>
/// Acquire a spinlock on a SPINLOCK object. This Disables the Scheduler (pre-emption) (use spinlock_init before acquiring the lock)
/// </summary>
/// <param name="lock">Pointer to SPINLOCK object.</param>
static void MtAcquireSpinlock(SPINLOCK* lock, IRQL* old_irql) {
	tracelast_func("MtAcquireSpinlock");
	if (!lock) return;
	// spin until we grab the lock.
	MtRaiseIRQL(DISPATCH_LEVEL, old_irql);
	while (__sync_lock_test_and_set(&lock->locked, 1)) {
		__asm__ volatile("pause" ::: "memory"); /* x86 pause — CPU relax hint */
	}
	// Memory barrier to prevent instruction reordering
	__asm__ volatile("" ::: "memory");
}

/// <summary>
/// Release the spinlock on a SPINLOCK object. This Enables the Scheduler (pre-emption)
/// </summary>
/// <param name="lock">Pointer to SPINLOCK object.</param>
static void MtReleaseSpinlock(SPINLOCK* lock, IRQL old_irql) {
	tracelast_func("MtReleaseSpinlock");
	if (!lock) return;
	// Memory barrier before release
	__asm__ volatile("" ::: "memory");
	__sync_lock_release(&lock->locked);
	MtLowerIRQL(old_irql);
}

#endif