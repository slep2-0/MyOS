/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      IRQL Implementation for MatanelOS.
 */

#include "irql.h"

IRQL irq_irql[16] = {
    DISPATCH_LEVEL,   // IRQ0 - Timer (must keep timer enabled at DISPATCH_LEVEL)
    DEVICE_LEVEL,     // IRQ1 - Keyboard
    DEVICE_LEVEL,     // IRQ2 - Cascade (usually for IRQs 8-15, treat as DEVICE_LEVEL)
    DEVICE_LEVEL,     // IRQ3 - Serial COM2
    DEVICE_LEVEL,     // IRQ4 - Serial COM1
    DEVICE_LEVEL,     // IRQ5 - Sound Card / LPT2
    DEVICE_LEVEL,     // IRQ6 - Floppy Disk
    DEVICE_LEVEL,     // IRQ7 - LPT1 / Printer
    DEVICE_LEVEL,     // IRQ8 - RTC / CMOS Alarm
    DEVICE_LEVEL,     // IRQ9 - Free for peripherals (often redirected cascade)
    DEVICE_LEVEL,     // IRQ10 - Free for peripherals
    DEVICE_LEVEL,     // IRQ11 - Free for peripherals
    DEVICE_LEVEL,     // IRQ12 - Mouse
    DEVICE_LEVEL,     // IRQ13 - FPU / Coprocessor / Inter-processor
    DEVICE_LEVEL,     // IRQ14 - Primary ATA Channel
    DEVICE_LEVEL      // IRQ15 - Secondary ATA Channel
};

static IRQL currentirql = PASSIVE_LEVEL;

static void apply_masking_for_irql(IRQL level) {
    // Mask any IRQ whose assigned IRQL is < current IRQL.
    // Unmask those >= current IRQL.
    for (uint8_t i = 0; i < 16; i++) {
        if (irq_irql[i] < level)
            mask_irq(i);
        else
            unmask_irq(i);
    }
}

void GetCurrentIRQL(IRQL* out) {
    tracelast_func("GetCurrentIRQL");
    *out = currentirql;
}

void RaiseIRQL(IRQL new_irql, IRQL* old_irql) {
    tracelast_func("RaiseIRQL");
    *old_irql = currentirql;

    if (new_irql < currentirql) {
        REGS regs;
        read_registers(&regs);
        bugcheck_system(&regs, IRQL_NOT_LESS_OR_EQUAL, 0, false);
        return;
    }

    // first, raise the IRQL
    currentirql = new_irql;

    // then apply masking
    apply_masking_for_irql(new_irql);

    // enable interrupts if we're below HIGH_LEVEL
    if (new_irql < HIGH_LEVEL)
        __sti();
    else
        __cli();
}

void LowerIRQL(IRQL new_irql, IRQL* old_irql) {
    tracelast_func("LowerIRQL");
    *old_irql = currentirql;

    if (new_irql > currentirql) {
        REGS regs;
        read_registers(&regs);
        bugcheck_system(&regs, IRQL_NOT_LESS_OR_EQUAL, 0, false);
        return;
    }

    // first, set the new IRQL
    currentirql = new_irql;

    // reapply masking for the lower level
    apply_masking_for_irql(new_irql);

    // enable interrupts if we're below HIGH_LEVEL
    if (new_irql < HIGH_LEVEL)
        __sti();
    else
        __cli();
}

// INTERNAL USE, BEWARE.
void _SetIRQL(IRQL new_irql) {
    tracelast_func("SetIRQL");

    currentirql = new_irql;
    apply_masking_for_irql(new_irql);

    if (new_irql < HIGH_LEVEL)
        __sti();
    else
        __cli();
}

void enforce_max_irql(IRQL max_allowed) {
    IRQL cur;
    GetCurrentIRQL(&cur);
    if (cur > max_allowed) {
        REGS regs;
        read_registers(&regs);
        bugcheck_system(&regs, IRQL_NOT_LESS_OR_EQUAL, cur, false);
    }
}
