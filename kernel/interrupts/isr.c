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

#ifndef _MSC_VER
__attribute__((used))
#endif
void isr_handler64(int vec_num, REGS* r) {
    IRQL oldIrql;

    switch (vec_num) {
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
    case TIMER_INTERRUPT:
        RaiseIRQL(CLOCK_LEVEL, &oldIrql);
        timer_handler();
        LowerIRQL(oldIrql);
        break;
    case KEYBOARD_INTERRUPT:
        RaiseIRQL(DIRQL_KEYBOARD, &oldIrql);
        keyboard_handler();
        LowerIRQL(oldIrql);
        break;
    case ATA_INTERRUPT:
        RaiseIRQL(DIRQL_PRIMARY_ATA, &oldIrql);
        ata_handler();
        LowerIRQL(oldIrql);
        break;
    default:
        gop_printf(&gop_local, 0xFFFF0000, "Interrupt Exception: ");
        gop_printf(&gop_local, 0xFFFFFFFF, "%d\r\n", vec_num);
        break;
    }
}

void init_interrupts() {
	install_idt();
    _SetIRQL(PASSIVE_LEVEL);
}