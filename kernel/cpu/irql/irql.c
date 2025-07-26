/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      IRQL Implementation for MatanelOS.
 */

#include "irql.h"
#include "../../bugcheck/bugcheck.h"
#include "../../interrupts/idt.h"

 // IRQ → DIRQL mapping (legacy non‑APIC: IRQn + DIRQL = PROFILE_LEVEL (27))
IRQL irq_irql[16] = {
    DIRQL_TIMER,          // IRQ0  – System Timer (27) -- highest.
    DIRQL_KEYBOARD,       // IRQ1  – Keyboard (26)
    DIRQL_CASCADE,        // IRQ2  – Cascade (IRQs 8–15) (25)
    DIRQL_COM2,           // IRQ3  – Serial COM2 (24)
    DIRQL_COM1,           // IRQ4  – Serial COM1 (23)
    DIRQL_SOUND_LPT2,     // IRQ5  – Sound Card / LPT2 (22)
    DIRQL_FLOPPY,         // IRQ6  – Floppy Disk (21)
    DIRQL_LPT1,           // IRQ7  – LPT1 / Printer (20)
    DIRQL_RTC,            // IRQ8  – RTC / CMOS Alarm (19)
    DIRQL_PERIPHERAL9,    // IRQ9  – Free / redirected cascade (18)
    DIRQL_PERIPHERAL10,   // IRQ10 – Free for peripherals (17)
    DIRQL_PERIPHERAL11,   // IRQ11 – Free for peripherals (16)
    DIRQL_MOUSE,          // IRQ12 – Mouse (15)
    DIRQL_FPU,            // IRQ13 – FPU / Coprocessor (14)
    DIRQL_PRIMARY_ATA,    // IRQ14 – Primary ATA Channel (13)
    DIRQL_SECONDARY_ATA   // IRQ15 – Secondary ATA Channel (12)
};

#define IRQ_LINES  (sizeof(irq_irql) / sizeof(irq_irql[0]))

static void apply_masking_for_irql(IRQL level) {
    // Mask any IRQ whose assigned IRQL is < current IRQL.
    // Unmask those >= current IRQL.
    for (uint8_t i = 0; i < IRQ_LINES; i++) {
        if (irq_irql[i] < level)
            mask_irq(i);
        else
            unmask_irq(i);
    }
}

void GetCurrentIRQL(IRQL* out) {
    tracelast_func("GetCurrentIRQL");
    *out = cpu.currentIrql;
}

void RaiseIRQL(IRQL new_irql, IRQL* old_irql) {
    tracelast_func("RaiseIRQL");
    *old_irql = cpu.currentIrql;

    if (new_irql < cpu.currentIrql) {
        REGS regs;
        read_registers(&regs);
        bugcheck_system(&regs, IRQL_NOT_LESS_OR_EQUAL, 0, false);
        return;
    }

    // first, raise the IRQL
    cpu.currentIrql = new_irql;

    // then apply masking
    apply_masking_for_irql(new_irql);

    // enable interrupts if we're below HIGH_LEVEL
    if (new_irql < HIGH_LEVEL)
        __sti();
    else
        __cli();
}

void LowerIRQL(IRQL new_irql) {
    tracelast_func("LowerIRQL");
    if (new_irql > cpu.currentIrql) {
        REGS regs;
        read_registers(&regs);
        bugcheck_system(&regs, IRQL_NOT_LESS_OR_EQUAL, 0, false);
        return;
    }

    // first, set the new IRQL
    cpu.currentIrql = new_irql;

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

    cpu.currentIrql = new_irql;
    apply_masking_for_irql(new_irql);

    if (new_irql < HIGH_LEVEL)
        __sti();
    else
        __cli();
}

void enforce_max_irql(IRQL max_allowed) {
    if (cpu.currentIrql > max_allowed) {
        REGS regs;
        read_registers(&regs);
        bugcheck_system(&regs, IRQL_NOT_LESS_OR_EQUAL, 0, false);
    }
}
