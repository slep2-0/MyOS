/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      IRQL Implementation (Fixed)
 */

#include "irql.h"
#include "../../bugcheck/bugcheck.h"
#include "../../interrupts/idt.h"

 // This is your existing mapping, it is correct.
IRQL irq_irql[16] = {
    DIRQL_TIMER, DIRQL_KEYBOARD, DIRQL_CASCADE, DIRQL_COM2,
    DIRQL_COM1, DIRQL_SOUND_LPT2, DIRQL_FLOPPY, DIRQL_LPT1,
    DIRQL_RTC, DIRQL_PERIPHERAL9, DIRQL_PERIPHERAL10, DIRQL_PERIPHERAL11,
    DIRQL_MOUSE, DIRQL_FPU, DIRQL_PRIMARY_ATA, DIRQL_SECONDARY_ATA
};

#define IRQ_LINES (sizeof(irq_irql) / sizeof(irq_irql[0]))

// This helper function now correctly represents its single purpose.
void update_pic_mask_for_current_irql(void) {
    tracelast_func("update_pic_mask_for_current_irql");
    IRQL level = cpu.currentIrql;

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
}

void GetCurrentIRQL(IRQL* out) {
    tracelast_func("GetCurrentIRQL");
    if (out) *out = cpu.currentIrql;
}

void RaiseIRQL(IRQL new_irql, IRQL* old_irql) {
    tracelast_func("RaiseIRQL");

    // It's good practice to disable interrupts for the duration of the change.
    // The CPU automatically does this when entering an ISR.
    // We do it here to ensure atomicity.
    __cli();

    if (old_irql) {
        *old_irql = cpu.currentIrql;
    }

    if (new_irql < cpu.currentIrql) {
        // You cannot "raise" to a lower level. This is a fatal kernel bug.
        bugcheck_system(NULL, NULL, IRQL_NOT_LESS_OR_EQUAL, 0, false);
    }

    cpu.currentIrql = new_irql;
    update_pic_mask_for_current_irql();

    // We do NOT call __sti() here. We let the caller (or iretq) handle it.
    // In the case of Schedule(), interrupts are already enabled, so we restore that state.
    __sti();
}

void LowerIRQL(IRQL new_irql) {
    tracelast_func("LowerIRQL");

    // Atomically update the IRQL and PIC mask.
    __cli();

    if (new_irql > cpu.currentIrql) {
        // You cannot "lower" to a higher level. This is a fatal kernel bug.
        bugcheck_system(NULL, NULL, IRQL_NOT_LESS_OR_EQUAL, 0, false);
    }

    cpu.currentIrql = new_irql;
    update_pic_mask_for_current_irql();

    // Now that the masks are correctly set for the lower IRQL,
    // we can safely re-enable interrupts.
    __sti();
}

// This function should be used sparingly, only during initialization.
void _SetIRQL(IRQL new_irql) {
    tracelast_func("_SetIRQL");
    __cli();
    cpu.currentIrql = new_irql;
    update_pic_mask_for_current_irql();
    __sti();
}

void enforce_max_irql(IRQL max_allowed) {
    tracelast_func("enforce_max_irql");
    if (cpu.currentIrql > max_allowed) {
        bugcheck_system(NULL, NULL, IRQL_NOT_LESS_OR_EQUAL, 0, false);
    }
}