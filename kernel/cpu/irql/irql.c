/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      IRQL Implementation (Fixed with Dispatch Level scheduling toggle)
 */

#include "irql.h"
#include "../../bugcheck/bugcheck.h"
#include "../../interrupts/idt.h"

IRQL irq_irql[16] = {
    DIRQL_TIMER, DIRQL_KEYBOARD, DIRQL_CASCADE, DIRQL_COM2,
    DIRQL_COM1, DIRQL_SOUND_LPT2, DIRQL_FLOPPY, DIRQL_LPT1,
    DIRQL_RTC, DIRQL_PERIPHERAL9, DIRQL_PERIPHERAL10, DIRQL_PERIPHERAL11,
    DIRQL_MOUSE, DIRQL_FPU, DIRQL_PRIMARY_ATA, DIRQL_SECONDARY_ATA
};

#define IRQ_LINES (sizeof(irq_irql) / sizeof(irq_irql[0]))

static inline bool interrupts_enabled(void) {
    unsigned long flags;
    __asm__ __volatile__("pushfq; popq %0" : "=r"(flags));
    return (flags & (1UL << 9)) != 0; // IF is bit 9
}

// IMPORTANT: We disable interrupts around PIC updates to avoid races.
void update_pic_mask_for_current_irql(void) {
    bool prev_if = interrupts_enabled();
    __cli();
    IRQL level = thisCPU()->currentIrql;

    // Mask any IRQ whose assigned IRQL is <= the current CPU IRQL.
    // Unmask any IRQ whose assigned IRQL is > the current CPU IRQL.
    for (uint8_t i = 0; i < IRQ_LINES; i++) {
        if (irq_irql[i] <= level) {
            mask_irq(i);
        }
        else {
            unmask_irq(i);
        }
    }
    if (prev_if) __sti();
}

static inline void toggle_scheduler(void) {
    // schedulerEnabled should be true only at IRQL < DISPATCH_LEVEL
    thisCPU()->schedulerEnabled = (thisCPU()->currentIrql < DISPATCH_LEVEL);
}

void MtGetCurrentIRQL(IRQL* out) {
    tracelast_func("GetCurrentIRQL");
    *out = atomic_load_explicit(&thisCPU()->currentIrql, memory_order_acquire);
}

void MtRaiseIRQL(IRQL new_irql, IRQL* old_irql) {
    bool prev_if = interrupts_enabled();
    __cli();
    tracelast_func("RaiseIRQL");

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
    update_pic_mask_for_current_irql();
    if (prev_if) __sti();
}

void MtLowerIRQL(IRQL new_irql) {
    bool prev_if = interrupts_enabled();
    __cli();
    tracelast_func("LowerIRQL");

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
    update_pic_mask_for_current_irql();
    if (prev_if) __sti();
}

// This function should be used sparingly, only during initialization.
void _MtSetIRQL(IRQL new_irql) {
    bool prev_if = interrupts_enabled();
    __cli();
    tracelast_func("_SetIRQL");

    thisCPU()->currentIrql = new_irql;
    toggle_scheduler();
    update_pic_mask_for_current_irql();
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
