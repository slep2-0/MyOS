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
void isr_handler64(int vec_num, INTERRUPT_FULL_REGS* r) {
    IRQL oldIrql;
    CTX_FRAME ctx;
    INT_FRAME intfr;

    // Since we are now using both a registration context, and an interrupt context, for compatibility I didn't change the asm stub, and just created 3 structs, 2 are the ctx and int, third is both (legacy).

    // Copy the register context
    kmemcpy(&ctx, r, sizeof(CTX_FRAME));  // Copies up to rsp

    // Copy the interrupt frame info
    kmemcpy(&intfr, &r->vector, sizeof(INT_FRAME));  // Start at vector, which follows rsp

    switch (vec_num) {
    case EXCEPTION_DIVIDE_BY_ZERO:
        dividebyzero_handler(&ctx, &intfr);
        break;
    case EXCEPTION_SINGLE_STEP:
        debugsinglestep_handler(&ctx, &intfr);
        break;
    case EXCEPTION_NON_MASKABLE_INTERRUPT:
        _SetIRQL(HIGH_LEVEL); // Non Maskable Interrupt - basically when the CPU encounters a hardware fault, cannot be masked, very alarming.
        nmi_handler(&ctx, &intfr);
        break;
    case EXCEPTION_BREAKPOINT:
        breakpoint_handler(&ctx, &intfr);
        break;
    case EXCEPTION_OVERFLOW:
        overflow_handler(&ctx, &intfr);
        break;
    case EXCEPTION_BOUNDS_CHECK:
        boundscheck_handler(&ctx, &intfr);
        break;
    case EXCEPTION_INVALID_OPCODE:
        invalidopcode_handler(&ctx, &intfr);
        break;
    case EXCEPTION_NO_COPROCESSOR:
        nocoprocessor_handler(&ctx, &intfr);
        break;
    case EXCEPTION_DOUBLE_FAULT:
        doublefault_handler(&ctx, &intfr);
        break;
    case EXCEPTION_COPROCESSOR_SEGMENT_OVERRUN:
        coprocessor_segment_overrun_handler(&ctx, &intfr);
        break;
    case EXCEPTION_SEGMENT_SELECTOR_NOTPRESENT:
        segment_selector_not_present_handler(&ctx, &intfr);
        break;
    case EXCEPTION_INVALID_TSS:
        invalidtss_handler(&ctx, &intfr);
        break;
    case EXCEPTION_GENERAL_PROTECTION_FAULT:
        gpf_handler(&ctx, &intfr);
        break;
    case EXCEPTION_PAGE_FAULT:
        pagefault_handler(&ctx, &intfr);
        break;
    case EXCEPTION_RESERVED:
        // reserved, do not use.
        break;
    case EXCEPTION_FLOATING_POINT_ERROR:
        fpu_handler(&ctx, &intfr);
        break;
    case EXCEPTION_ALIGNMENT_CHECK:
        alignment_check_handler(&ctx, &intfr);
        break;
    case EXCEPTION_SEVERE_MACHINE_CHECK:
        _SetIRQL(HIGH_LEVEL); // machine check, like NMI, high irql.
        severe_machine_check_handler(&ctx, &intfr);
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