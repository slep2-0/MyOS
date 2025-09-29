/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      IRQL Implementation (Fixed with Dispatch Level scheduling toggle)
 */

#include "irql.h"
#include "../bugcheck/bugcheck.h"
#include "../interrupts/idt.h"

// Taken from ReactOS, supports up to 1 IOAPIC.
// Instead of masking PICs, we now use the CR8 register (or in 32bit, the MMIO of LAPIC + 0x80), to use the TPR
// The TPR (Task Priority Register), is a special register in the Local APIC that determines which interrupts are allowed to be delivered to the CPU.
// We use IRQL, and that IRQL translates that to TPR Levels with the macros below.
#define IRQ2VECTOR(irq)		((irq) + 0x0) // 0 is the starting point

#define IRQL2VECTOR(irql)   (IRQ2VECTOR(PROFILE_LEVEL - (irql)))

// If its IPI_LEVEL, we use the IDT vector for the IPIs, same thing from PROFILE which is the LAPIC TIMER, and anything else (like DIRQLs), we use the IRQL2VECTOR
#define IRQL2TPR(irql)	    ((irql) >= IPI_LEVEL ? LAPIC_ACTION_VECTOR : ((irql) >= PROFILE_LEVEL ? LAPIC_INTERRUPT : ((irql) > DISPATCH_LEVEL ? IRQL2VECTOR(irql) : 0)))

static inline bool interrupts_enabled(void) {
    unsigned long flags;
    __asm__ __volatile__("pushfq; popq %0" : "=r"(flags));
    return (flags & (1UL << 9)) != 0; // IF is bit 9
}

static void update_apic_irqs(IRQL newLevel) {
    __write_cr8(IRQL2TPR(newLevel));
}

static inline void toggle_scheduler(void) {
    // schedulerEnabled should be true only at IRQL < DISPATCH_LEVEL
    thisCPU()->schedulerEnabled = (thisCPU()->currentIrql < DISPATCH_LEVEL);
}

void MtGetCurrentIRQL(IRQL* out) {
    tracelast_func("MtGetCurrentIRQL");
    *out = atomic_load_explicit(&thisCPU()->currentIrql, memory_order_acquire);
}

void MtRaiseIRQL(IRQL new_irql, IRQL* old_irql) {
    bool prev_if = interrupts_enabled();
    __cli();
    tracelast_func("MtRaiseIRQL");

    if (old_irql) {
        *old_irql = thisCPU()->currentIrql;
    }

    IRQL curr = atomic_load_explicit(&thisCPU()->currentIrql, memory_order_acquire);
    if (new_irql < curr) {
        CTX_FRAME ctx;
        SAVE_CTX_FRAME(&ctx);
        BUGCHECK_ADDITIONALS addt = { 0 };
        ksnprintf(addt.str, sizeof(addt.str), "Attempted to raise IRQL to a lower level than current IRQL.");
        MtBugcheckEx(&ctx, NULL, IRQL_NOT_GREATER_OR_EQUAL, &addt, true);
    }

    thisCPU()->currentIrql = new_irql;
    toggle_scheduler();
    if (new_irql > DISPATCH_LEVEL) {
        update_apic_irqs(new_irql);
    }
    if (prev_if) __sti();
}

void MtLowerIRQL(IRQL new_irql) {
    bool prev_if = interrupts_enabled();
    __cli();
    tracelast_func("MtLowerIRQL");

    IRQL curr = atomic_load_explicit(&thisCPU()->currentIrql, memory_order_acquire);
    if (new_irql > curr) {
        CTX_FRAME ctx;
        SAVE_CTX_FRAME(&ctx);
        BUGCHECK_ADDITIONALS addt = { 0 };
        ksnprintf(addt.str, sizeof(addt.str), "Attempted to lower IRQL to a higher level than current IRQL.");
        MtBugcheckEx(&ctx, NULL, IRQL_NOT_LESS_OR_EQUAL, &addt, true);
    }

    thisCPU()->currentIrql = new_irql;
    toggle_scheduler();
    if (new_irql > DISPATCH_LEVEL) {
        update_apic_irqs(new_irql);
    }
    if (prev_if) __sti();
}

// This function should be used sparingly, only during initialization.
void _MtSetIRQL(IRQL new_irql) {
    bool prev_if = interrupts_enabled();
    __cli();
    tracelast_func("_SetIRQL");

    thisCPU()->currentIrql = new_irql;
    toggle_scheduler();
    if (new_irql > DISPATCH_LEVEL)  {
        update_apic_irqs(new_irql);
    }
    if (prev_if) __sti();
}

inline void enforce_max_irql(IRQL max_allowed, void* RIP) {
    bool prev_if = interrupts_enabled();
    __cli();
    IRQL curr = atomic_load_explicit(&thisCPU()->currentIrql, memory_order_acquire);
    if (curr > max_allowed) {
        CTX_FRAME ctx;
        SAVE_CTX_FRAME(&ctx);
        BUGCHECK_ADDITIONALS addt = { 0 };
        ksnprintf(addt.str, sizeof(addt.str), "Function was called above its maximum IRQL limit.");
        addt.ptr = RIP;
        MtBugcheckEx(&ctx, NULL, IRQL_NOT_LESS_OR_EQUAL, &addt, true);
    }
    if (prev_if) __sti();
}
