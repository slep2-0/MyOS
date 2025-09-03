/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      Spinlock Types and Function Declarations.
 */
#ifndef X86_SPINLOCK_H
#define X86_SPINLOCK_H

// Supress intelliSense
#ifdef _MSC_VER
#undef atomic_flag_clear
#define atomic_flag_clear
#undef atomic_flag_test_and_set_explicit
#define atomic_flag_test_and_set_explicit
#undef atomic_flag_clear_explicit
#define atomic_flag_clear_explicit
#endif

static inline uint64_t save_and_cli(void) {
	uint64_t flags;
	__asm__ volatile (
		"pushfq\n\t"
		"popq %0\n\t"
		"cli"
		: "=r"(flags)
		:: "memory"
		);
	return flags;
}

static inline void restore_flags(uint64_t flags) {
	__asm__ volatile (
		"pushq %0\n\t"
		"popfq"
		:: "r"(flags)
		: "memory", "cc"
		);
}

/// <summary>
/// To be used only if SPINLOCK is a stack variable.
/// </summary>
/// <param name="lock">SPINLOCK Object pointer.</param>
static inline void spinlock_init(SPINLOCK* lock) {
	if (!lock) return;
	atomic_flag_clear(&lock->LOCKED);
}

/// <summary>
/// Acquire a spinlock on a SPINLOCK object. BE VERY CAREFUL: THIS DISABLES INTERRUPTS.
/// </summary>
/// <param name="lock">Pointer to SPINLOCK object.</param>
static inline void MtAcquireSpinlock(SPINLOCK* lock, uint64_t* flags_out) {
	tracelast_func("MtAcquireSpinlock");
	if (!lock) return;
	// spin until we grab the lock.
	*flags_out = save_and_cli();
	while (atomic_flag_test_and_set_explicit(&lock->LOCKED, memory_order_acquire)) {
		//__asm__ volatile ("pause");
		__builtin_ia32_pause();
	}
}

/// <summary>
/// Release the spinlock on a SPINLOCK object. THIS ENABLES INTERRUPTS. (if were disabled)
/// </summary>
/// <param name="lock">Pointer to SPINLOCK object.</param>
static inline void MtReleaseSpinlock(SPINLOCK* lock, uint64_t flags) {
	tracelast_func("MtReleaseSpinlock");
	if (!lock) return;
	atomic_flag_clear_explicit(&lock->LOCKED, memory_order_release);
	restore_flags(flags);
}

#endif
