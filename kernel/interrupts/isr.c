/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:		IMPLEMENTATION To SETUP ISR Handler.
 * EXPLANATION: An ISR is what handles the interrupts that gets sent from the CPU (after interrupt is sent to ISR itself), it will do stuff based if it's an exception, or a normal interrupt.
 */

#include "idt.h"

const bool has_error_code[] = {
    false, false, false, false, false, false, false, false, // 0-7
    true,  false, true,  true,  true,  true,  true,  false, // 8-15
    false, false, false, false, false, false, false, false, // 16-23
    false, false, false, false, false, false, false, false  // 24-31
};

void isr_handler(int vec_num, REGS* r) {
    switch (vec_num) {
    case EXCEPTION_DIVIDE_BY_ZERO:
        dividebyzero_handler();
        return;
    case EXCEPTION_SINGLE_STEP:
        debugsinglestep_handler();
        return;
    case EXCEPTION_NON_MASKABLE_INTERRUPT:
        nmi_handler();
        return;
    case EXCEPTION_BREAKPOINT:
        breakpoint_handler();
        return;
    case EXCEPTION_OVERFLOW:
        overflow_handler();
        return;
    case EXCEPTION_BOUNDS_CHECK:
        boundscheck_handler();
        return;
    case EXCEPTION_INVALID_OPCODE:
        invalidopcode_handler();
        return;
    case EXCEPTION_NO_COPROCESSOR:
        nocoprocessor_handler();
        return;
    case EXCEPTION_DOUBLE_FAULT:
        doublefault_handler();
        return;
    case EXCEPTION_COPROCESSOR_SEGMENT_OVERRUN:
        coprocessor_segment_overrun_handler();
        return;
    case EXCEPTION_SEGMENT_SELECTOR_NOTPRESENT:
        segment_selector_not_present_handler();
        return;
    case EXCEPTION_INVALID_TSS:
        invalidtss_handler();
        return;
    case EXCEPTION_GENERAL_PROTECTION_FAULT:
        gpf_handler(r);
        return;
    case EXCEPTION_PAGE_FAULT:
        pagefault_handler(r->error_code);
        return;
    case EXCEPTION_RESERVED:
        // reserved, do not use.
        return;
    case EXCEPTION_FLOATING_POINT_ERROR:
        fpu_handler();
        return;
    case EXCEPTION_ALIGNMENT_CHECK:
        alignment_check_handler();
        return;
    case EXCEPTION_SEVERE_MACHINE_CHECK:
        severe_machine_check_handler();
        return;
    case KEYBOARD_INTERRUPT:
        keyboard_handler();
        return;
    case TIMER_INTERRUPT:
        timer_handler();
        return;
    default:
        print_to_screen("Interrupt Exception: ", COLOR_RED);
        print_dec(vec_num, COLOR_WHITE);
        print_to_screen(" \r\n", COLOR_BLACK);
        return;
    }
}

void init_interrupts() {
	install_idt();
}