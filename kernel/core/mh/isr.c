/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:		IMPLEMENTATION To SETUP ISR Handler.
 * EXPLANATION: An ISR is what handles the interrupts that gets sent from the CPU (after interrupt is sent to ISR itself), it will do stuff based if it's an exception, or a normal interrupt.
 */

#include "../../includes/mh.h"
#include "../../includes/mg.h"
#include "../../trace.h"

extern GOP_PARAMS gop_local;

const bool has_error_code[] = {
    false, false, false, false, false, false, false, false, // 0-7
    true,  false, true,  true,  true,  true,  true,  false, // 8-15
    false, false, false, false, false, false, false, false, // 16-23
    false, false, false, false, false, false, false, false  // 24-31
};

extern void lapic_eoi(void);    

USED
void
MhHandleInterrupt (
    IN  int vec_num, 
    IN  PTRAP_FRAME trap
) 

/*++

    Routine description : This function handles all traps, exceptions, and interrupts of the processor, and forwards them to the appropriate function.

    Arguments:

        [IN]    int vec_num: Vector number that represents the interrupt.
        [IN]    PTRAP_FRAME trap: Pointer to trap frame, saved by the stub.

    Return Values:

        None.

--*/

{
    char buf[256];
    ksnprintf(buf, sizeof(buf), "INTERRUPT: %d", vec_num);
    tracelast_func(buf);
    IRQL oldIrql;
    
    // Save if the scheduler was enabled or not before raising to >= DISPATCH_LEVEL (because in dispatch_level and above the scheduler gets disabled to disable pre-emption)
    bool schedulerEnabled = MeGetCurrentProcessor()->schedulerEnabled;

    switch (vec_num) {
    case EXCEPTION_DIVIDE_BY_ZERO:
        MiDivideByZero(trap);
        break;
    case EXCEPTION_SINGLE_STEP:
        MiDebugTrap(trap);
        break;
    case EXCEPTION_NON_MASKABLE_INTERRUPT:
        _MeSetIrql(HIGH_LEVEL); // Non Maskable Interrupt - basically when the CPU encounters a hardware fault, cannot be masked, very alarming.
        MiNonMaskableInterrupt(trap);
        break;
    case EXCEPTION_BREAKPOINT:
        MiBreakpoint(trap);
        break;
    case EXCEPTION_OVERFLOW:
        MiOverflow(trap);
        break;
    case EXCEPTION_BOUNDS_CHECK:
        MiBoundsCheck(trap);
        break;
    case EXCEPTION_INVALID_OPCODE:
        MiInvalidOpcode(trap);
        break;
    case EXCEPTION_NO_COPROCESSOR:
        MiNoCoprocessor(trap);
        break;
    case EXCEPTION_DOUBLE_FAULT:
        _MeSetIrql(HIGH_LEVEL);
        MiDoubleFault(trap);
        break;
    case EXCEPTION_COPROCESSOR_SEGMENT_OVERRUN:
        MiCoprocessorSegmentOverrun(trap);
        break;
    case EXCEPTION_SEGMENT_SELECTOR_NOTPRESENT:
        MiSegmentSelectorNotPresent(trap);
        break;
    case EXCEPTION_INVALID_TSS:
        MiInvalidTss(trap);
        break;
    case EXCEPTION_GENERAL_PROTECTION_FAULT:
        MiGeneralProtectionFault(trap);
        break;
    case EXCEPTION_PAGE_FAULT:
        MiPageFault(trap);
        break;
    case EXCEPTION_RESERVED:
        // reserved, do not use.
        break;
    case EXCEPTION_FLOATING_POINT_ERROR:
        MiFloatingPointError(trap);
        break;
    case EXCEPTION_ALIGNMENT_CHECK:
        MiAlignmentCheck(trap);
        break;
    case EXCEPTION_SEVERE_MACHINE_CHECK:
        _MeSetIrql(HIGH_LEVEL); // machine check, like NMI, high irql.
        MiMachineCheck(trap);
        break;
    case LAPIC_ACTION_VECTOR:
        MeRaiseIrql(IPI_LEVEL, &oldIrql);
        MiInterprocessorInterrupt();
        MeLowerIrql(oldIrql);
        break;
    case LAPIC_INTERRUPT:
        MeRaiseIrql(CLOCK_LEVEL, &oldIrql);
        MiLapicInterrupt(schedulerEnabled, trap);
        MeLowerIrql(oldIrql);
        break;
    case LAPIC_SIV_INTERRUPT:
        // just send EOI
        lapic_eoi();
        break;
    default:
        gop_printf(0xFFFF0000, "Interrupt Exception: %d\n", vec_num);
        break;
    }
}

void init_interrupts() {
	install_idt();
    _MeSetIrql(PASSIVE_LEVEL);
}
