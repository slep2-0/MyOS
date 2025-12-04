/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:		IMPLEMENTATION To SETUP ISR Handler.
 * EXPLANATION: An ISR is what handles the interrupts that gets sent from the CPU (after interrupt is sent to ISR itself), it will do stuff based if it's an exception, or a normal interrupt.
 */

#include "../../includes/core.h"
#include "../../includes/mh.h"
#include "../../includes/mg.h"
#include "../../includes/me.h"
#include "../../assert.h"

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

    assert(MeAreInterruptsEnabled() == false);

    PPROCESSOR cpu = MeGetCurrentProcessor();
    IRQL oldIrql;
    
    // Save if the scheduler was enabled or not before raising to >= DISPATCH_LEVEL (because in dispatch_level and above the scheduler gets disabled to disable pre-emption)
    bool schedulerEnabled = cpu->schedulerEnabled;

    // Save the PreviousMode to current thread.
    PRIVILEGE_MODE PreviousMode;

    if ((trap->cs & 0x3) == 0x3) {
        PreviousMode = UserMode;
    }
    else {
        PreviousMode = KernelMode;
    }
    
    if (cpu->currentThread) {
        cpu->currentThread->PreviousMode = PreviousMode;
    }

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
    case LAPIC_INTERRUPT:
        MeRaiseIrql(CLOCK_LEVEL, &oldIrql);
        MiLapicInterrupt(schedulerEnabled, trap);
        MeLowerIrql(oldIrql);
        break;
    case VECTOR_IPI:
        MeRaiseIrql(IPI_LEVEL, &oldIrql);
        MiInterprocessorInterrupt();
        lapic_eoi();
        MeLowerIrql(oldIrql);
        break;
    case VECTOR_DPC:
        // To see how is this triggered, check MeInsertQueueDpc or MeLowerIrql
        assert(MeAreInterruptsEnabled() == false);
        // Raise IRQL to DISPATCH_LEVEL
        MeRaiseIrql(DISPATCH_LEVEL, &oldIrql);
        // Drain DPCs while in DISPATCH
        MeRetireDPCs();
        // Send EOI, this is called by the APIC Self BIT (so LAPIC)
        lapic_eoi();
        // Lower IRQL back.
        MeLowerIrql(oldIrql);
        break;
    case LAPIC_SIV_INTERRUPT:
        // just send EOI
        lapic_eoi();
        break;
    default:
        break;
    }

    assert(MeAreInterruptsEnabled() == false);
    // Send An EOI
    // TODO KINTERRUPT
}

void init_interrupts() {
	install_idt();
    _MeSetIrql(PASSIVE_LEVEL);
}
