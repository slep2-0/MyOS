/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:		IMPLEMENTATION To SETUP ISR Handler.
 * EXPLANATION: An ISR is what handles the interrupts that gets sent from the CPU (after interrupt is sent to ISR itself), it will do stuff based if it's an exception, or a normal interrupt.
 */

#include "idt.h"
#include "../cpu/apic/apic.h"

extern GOP_PARAMS gop_local;

const bool has_error_code[] = {
    false, false, false, false, false, false, false, false, // 0-7
    true,  false, true,  true,  true,  true,  true,  false, // 8-15
    false, false, false, false, false, false, false, false, // 16-23
    false, false, false, false, false, false, false, false  // 24-31
};

#ifndef _MSC_VER
__attribute__((used))
#endif
void isr_handler64(int vec_num, CTX_FRAME* ctx, INT_FRAME* intfr) {
    char buf[256];
    ksnprintf(buf, sizeof(buf), "INTERRUPT: %d", vec_num);
    tracelast_func(buf);
    IRQL oldIrql;
    
    // Save if the scheduler was enabled or not before raising to >= DISPATCH_LEVEL (because in dispatch_level the scheduler gets disabled to disable pre-emption)
    bool schedulerEnabled = cpu.schedulerEnabled;

    ctx->rip = intfr->rip;
    ctx->rsp = intfr->rsp;
    intfr->vector = vec_num;

    switch (vec_num) {
    case EXCEPTION_DIVIDE_BY_ZERO:
        dividebyzero_handler(ctx, intfr);
        break;
    case EXCEPTION_SINGLE_STEP:
        debugsinglestep_handler(ctx, intfr);
        break;
    case EXCEPTION_NON_MASKABLE_INTERRUPT:
        _MtSetIRQL(HIGH_LEVEL); // Non Maskable Interrupt - basically when the CPU encounters a hardware fault, cannot be masked, very alarming.
        nmi_handler(ctx, intfr);
        break;
    case EXCEPTION_BREAKPOINT:
        breakpoint_handler(ctx, intfr);
        break;
    case EXCEPTION_OVERFLOW:
        overflow_handler(ctx, intfr);
        break;
    case EXCEPTION_BOUNDS_CHECK:
        boundscheck_handler(ctx, intfr);
        break;
    case EXCEPTION_INVALID_OPCODE:
        invalidopcode_handler(ctx, intfr);
        break;
    case EXCEPTION_NO_COPROCESSOR:
        nocoprocessor_handler(ctx, intfr);
        break;
    case EXCEPTION_DOUBLE_FAULT:
        _MtSetIRQL(HIGH_LEVEL);
        doublefault_handler(ctx, intfr);
        break;
    case EXCEPTION_COPROCESSOR_SEGMENT_OVERRUN:
        coprocessor_segment_overrun_handler(ctx, intfr);
        break;
    case EXCEPTION_SEGMENT_SELECTOR_NOTPRESENT:
        segment_selector_not_present_handler(ctx, intfr);
        break;
    case EXCEPTION_INVALID_TSS:
        invalidtss_handler(ctx, intfr);
        break;
    case EXCEPTION_GENERAL_PROTECTION_FAULT:
        gpf_handler(ctx, intfr);
        break;
    case EXCEPTION_PAGE_FAULT:
        pagefault_handler(ctx, intfr);
        break;
    case EXCEPTION_RESERVED:
        // reserved, do not use.
        break;
    case EXCEPTION_FLOATING_POINT_ERROR:
        fpu_handler(ctx, intfr);
        break;
    case EXCEPTION_ALIGNMENT_CHECK:
        alignment_check_handler(ctx, intfr);
        break;
    case EXCEPTION_SEVERE_MACHINE_CHECK:
        _MtSetIRQL(HIGH_LEVEL); // machine check, like NMI, high irql.
        severe_machine_check_handler(ctx, intfr);
        break;
    case KEYBOARD_INTERRUPT:
        MtRaiseIRQL(DIRQL_KEYBOARD, &oldIrql);
        keyboard_handler();
        MtLowerIRQL(oldIrql);
        break;
    case LAPIC_INTERRUPT:
        MtRaiseIRQL(DIRQL_TIMER, &oldIrql);
        lapic_handler(schedulerEnabled);
        MtLowerIRQL(oldIrql);
        break;
    case LAPIC_SIV_INTERRUPT:
        // just send EOI
        lapic_eoi();
        break;
    default:
        gop_printf(0xFFFF0000, "Interrupt Exception: ");
        gop_printf(0xFFFFFFFF, "%d\r\n", vec_num);
        break;
    }
}

void init_interrupts() {
	install_idt();
    _MtSetIRQL(PASSIVE_LEVEL);
}
