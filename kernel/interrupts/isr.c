/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:		IMPLEMENTATION To SETUP ISR Handler.
 * EXPLANATION: An ISR is what handles the interrupts that gets sent from the CPU (after interrupt is sent to ISR itself), it will do stuff based if it's an exception, or a normal interrupt.
 */

#include "idt.h"

extern GOP_PARAMS gop_local;

// DO NOT PUT TRACELAST_FUNC HERE.

const bool has_error_code[] = {
    false, false, false, false, false, false, false, false, // 0-7
    true,  false, true,  true,  true,  true,  true,  false, // 8-15
    false, false, false, false, false, false, false, false, // 16-23
    false, false, false, false, false, false, false, false  // 24-31
};

static IRQL irq_irql[16] = {
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

#ifndef _MSC_VER
__attribute__((used))
#endif
void isr_handler64(int vec_num, REGS* r) {

    IRQL irq_level = (vec_num < 16) ? irq_irql[vec_num] : PASSIVE_LEVEL;
    IRQL oldIrql;
    RaiseIRQL(irq_level, &oldIrql);

    switch (vec_num) {
    case TIMER_INTERRUPT:
        timer_handler();
        break;
    case EXCEPTION_DIVIDE_BY_ZERO:
        dividebyzero_handler(r);
        break;
    case EXCEPTION_SINGLE_STEP:
        debugsinglestep_handler(r);
        break;
    case EXCEPTION_NON_MASKABLE_INTERRUPT:
        nmi_handler(r);
        break;
    case EXCEPTION_BREAKPOINT:
        breakpoint_handler(r);
        break;
    case EXCEPTION_OVERFLOW:
        overflow_handler(r);
        break;
    case EXCEPTION_BOUNDS_CHECK:
        boundscheck_handler(r);
        break;
    case EXCEPTION_INVALID_OPCODE:
        invalidopcode_handler(r);
        break;
    case EXCEPTION_NO_COPROCESSOR:
        nocoprocessor_handler(r);
        break;
    case EXCEPTION_DOUBLE_FAULT:
        doublefault_handler(r);
        break;
    case EXCEPTION_COPROCESSOR_SEGMENT_OVERRUN:
        coprocessor_segment_overrun_handler(r);
        break;
    case EXCEPTION_SEGMENT_SELECTOR_NOTPRESENT:
        segment_selector_not_present_handler(r);
        break;
    case EXCEPTION_INVALID_TSS:
        invalidtss_handler(r);
        break;
    case EXCEPTION_GENERAL_PROTECTION_FAULT:
        gpf_handler(r);
        break;
    case EXCEPTION_PAGE_FAULT:
        pagefault_handler(r);
        break;
    case EXCEPTION_RESERVED:
        // reserved, do not use.
        break;
    case EXCEPTION_FLOATING_POINT_ERROR:
        fpu_handler(r);
        break;
    case EXCEPTION_ALIGNMENT_CHECK:
        alignment_check_handler(r);
        break;
    case EXCEPTION_SEVERE_MACHINE_CHECK:
        severe_machine_check_handler(r);
        break;
    case KEYBOARD_INTERRUPT:
        keyboard_handler();
        break;
    case ATA_INTERRUPT:
        ata_handler();
        break;
    default:
        gop_printf(&gop_local, 0xFFFF0000, "Interrupt Exception: ");
        gop_printf(&gop_local, 0xFFFFFFFF, "%d\r\n", vec_num);
        break;
    }
    LowerIRQL(oldIrql, &oldIrql);
}

void init_interrupts() {
	install_idt();
    _SetIRQL(PASSIVE_LEVEL);
}