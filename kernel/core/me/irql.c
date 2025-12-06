/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      IRQL Implementation (Fixed with Dispatch Level scheduling toggle)
 */

#include "../../includes/me.h"
#include "../../intrinsics/atomic.h"
#include "../../intrinsics/intrin.h"
#include <stdatomic.h>

static inline bool interrupts_enabled(void) {
    unsigned long flags;
    __asm__ __volatile__("pushfq; popq %0" : "=r"(flags));
    return (flags & (1UL << 9)) != 0; // IF is bit 9
}

static void update_apic_irqs(IRQL newLevel) {
    uint8_t tpr = 0;

    switch (newLevel) {
    case HIGH_LEVEL:
    case POWER_LEVEL:
    case IPI_LEVEL:
        tpr = 15; // block everything
        break;

    case CLOCK_LEVEL:
    case PROFILE_LEVEL:
        tpr = TPR_PROFILE; // e.g. 10
        break;

    case DISPATCH_LEVEL:
        tpr = TPR_DISPATCH; // numeric (e.g. 8)
        break;

    case PASSIVE_LEVEL:
    default:
        tpr = TPR_PASSIVE; // 0
        break;
    }

    __write_cr8((unsigned long)tpr);
}

static inline void toggle_scheduler(void) {
    // schedulerEnabled should be true only at IRQL < DISPATCH_LEVEL
    MeGetCurrentProcessor()->schedulerEnabled = (MeGetCurrentIrql() < DISPATCH_LEVEL);
}

// PUBLIC API

void 
MeRaiseIrql (
    IN IRQL NewIrql,
    OUT PIRQL OldIrql
) 

/*++

    Routine description : This function raises the current IRQL of the CPU to the specified 'NewIrql', and updates IRQL rules along with it (scheduler, APIC masks...).

    Arguments:
        
        [IN]    IRQL NewIrql: The new IRQL to set.
        [OUT]   PIRQL OldIrql: The old IRQL variable address.

    Return Values:
        
        None.

--*/

{
    bool prev_if = interrupts_enabled();
    __cli();

    if (OldIrql) {
        *OldIrql = MeGetCurrentProcessor()->currentIrql;
    }

    IRQL curr = MeGetCurrentIrql();
    if (NewIrql < curr) {
        MeBugCheck(IRQL_NOT_GREATER_OR_EQUAL);
    }

    MeGetCurrentProcessor()->currentIrql = NewIrql;
    toggle_scheduler();
    update_apic_irqs(NewIrql);
    if (prev_if) __sti();
}

void
MeLowerIrql (
   IN IRQL NewIrql
) 

/*++

    Routine description : 
    
        This function lowers the current IRQL of the CPU to the specified 'NewIrql', and updates IRQL rules along with it (scheduler, APIC masks...).
        
        N.B: The function checks if a software interrupt is pending AND that the interrupt IRQL pending is LOWER or EQUAL to current IRQL,
             if so, it will generate the interrupt, even on interrupts disabled.

    Arguments:

        [IN]    IRQL NewIrql: The new IRQL to set.

    Return Values:

        None.

--*/

{
    bool prev_if = interrupts_enabled();
    __cli();

    IRQL curr = MeGetCurrentIrql();
    if (NewIrql > curr) {
        MeBugCheck(IRQL_NOT_LESS_OR_EQUAL);
    }

    MeGetCurrentProcessor()->currentIrql = NewIrql;

    toggle_scheduler();
    update_apic_irqs(NewIrql);

    PPROCESSOR cpu = MeGetCurrentProcessor();
    MmFullBarrier();
    if (cpu->DpcInterruptRequested && !cpu->DpcRoutineActive && NewIrql <= DISPATCH_LEVEL) {
        MhRequestSoftwareInterrupt(DISPATCH_LEVEL);
    }

    // TODO Check for APC interrupt when APCs developed

    if (prev_if) __sti();
}

// This function should be used sparingly, only during initialization.
void 
_MeSetIrql (
    IN IRQL NewIrql
)

/*++

    Routine description : This function forcefully SETS (ignores bugcheck rules) the current IRQL of the CPU to the specified 'NewIrql', and updates IRQL rules along with it (scheduler, APIC masks...).

    Arguments:

        [IN]    IRQL NewIrql: The new IRQL to set.

    Return Values:

        None.
        
    Notes: 

        Use sparingly, this function ignores bugcheck IRQL rules.

--*/

{
    bool prev_if = interrupts_enabled();
    __cli();

    MeGetCurrentProcessor()->currentIrql = NewIrql;
    toggle_scheduler();
    update_apic_irqs(NewIrql);
    if (prev_if) __sti();
}

bool
MeDisableInterrupts(
    void
)

// Short Desc: Will disable interrupts, and returns if interrupts were enabled before.

{
    bool prev_if = interrupts_enabled();
    __cli();
    return prev_if;
}

void
MeEnableInterrupts(
    IN bool EnabledBefore
)

// Short Desc: Will enable interrupts ONLY if EnabledBefore is true. (given from return value of MeDisableInterrupts)

{
    if (EnabledBefore) __sti();
}

bool
MeAreInterruptsEnabled(
    void
)

// Short Desc: Will return if interrupts are currently enabled on the processor.

{
    return interrupts_enabled();
}