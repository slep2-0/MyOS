/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      IRQL Implementation (Fixed with Dispatch Level scheduling toggle)
 */

#include "../../includes/me.h"
#include "../../intrinsics/atomic.h"
#include "../../intrinsics/intrin.h"

static inline bool interrupts_enabled(void)

/*++

    Routine description:

        Reports whether maskable interrupts are enabled on the current processor.

    Arguments:

        None.

    Return Values:

        A nonzero value when the reported condition holds, or zero otherwise.

--*/

{
    unsigned long flags;
    __asm__ __volatile__("pushfq; popq %0" : "=r"(flags));
    return (flags & (1UL << 9)) != 0; // IF is bit 9
}

static void update_apic_irqs(IRQL newLevel)

/*++

    Routine description:

        Updates the local APIC task-priority state after an IRQL change.

    Arguments:

        [IN] newLevel - IRQL being installed on the current processor.

    Return Values:

        None.

--*/

{
    uint8_t tpr = 0;

    switch (newLevel) {
    case HIGH_LEVEL:
    case POWER_LEVEL:
        tpr = 15; // Max priority
        break;

    case IPI_LEVEL:
        tpr = (VECTOR_IPI >> 4);
        break;

    case CLOCK_LEVEL:
        tpr = (VECTOR_CLOCK >> 4);
        break;

    case DISPATCH_LEVEL:
        tpr = (VECTOR_DPC >> 4);
        break;

    case APC_LEVEL:
        tpr = (VECTOR_APC >> 4);
        break;

    case PASSIVE_LEVEL:
    default:
        tpr = 0;
        break;
    }

    __write_cr8((unsigned long)tpr);
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

#ifdef DEBUG
    IRQL curr = MeGetCurrentIrql();
    if (NewIrql < curr) {
        MeBugCheck(IRQL_NOT_GREATER_OR_EQUAL);
    }
#endif

    MeGetCurrentProcessor()->currentIrql = NewIrql;
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

#ifdef DEBUG
    IRQL curr = MeGetCurrentIrql();
    if (NewIrql > curr) {
        MeBugCheck(IRQL_NOT_LESS_OR_EQUAL);
    }
#endif

    MeGetCurrentProcessor()->currentIrql = NewIrql;

    update_apic_irqs(NewIrql);

    PPROCESSOR cpu = MeGetCurrentProcessor();
    MmFullBarrier();
    
    // First check for DPC Interrupts.
    if (InterlockedLoadAcquire(&cpu->DpcInterruptRequested) &&
        !InterlockedLoadAcquire(&cpu->DpcRoutineActive) &&
        NewIrql <= DISPATCH_LEVEL) {
        MhRequestSoftwareInterrupt(DISPATCH_LEVEL);
    }

    // Now APC Interrupts.
    PITHREAD Thread = MeGetCurrentThread();
    if (NewIrql <= APC_LEVEL && Thread &&
        InterlockedLoadAcquire(&Thread->ApcState.KernelApcPending) &&
        !cpu->ApcRoutineActive) {
        MhRequestSoftwareInterrupt(APC_LEVEL);
    }

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
    update_apic_irqs(NewIrql);

    PPROCESSOR cpu = MeGetCurrentProcessor();
    MmFullBarrier();
    if (InterlockedLoadAcquire(&cpu->DpcInterruptRequested) &&
        !InterlockedLoadAcquire(&cpu->DpcRoutineActive) &&
        NewIrql <= DISPATCH_LEVEL) {
        MhRequestSoftwareInterrupt(DISPATCH_LEVEL);
    }

    // Now APC Interrupts.
    PITHREAD Thread = MeGetCurrentThread();
    if (NewIrql <= APC_LEVEL && Thread &&
        InterlockedLoadAcquire(&Thread->ApcState.KernelApcPending) &&
        !cpu->ApcRoutineActive) {
        MhRequestSoftwareInterrupt(APC_LEVEL);
    }

    if (prev_if) __sti();
}

bool
MeDisableInterrupts(
    void
)

// Short Desc: Will disable interrupts, and returns if interrupts were enabled before.

/*++

    Routine description:

        Disables maskable interrupts and reports their previous state.

    Arguments:

        None.

    Return Values:

        true when interrupts were enabled before the call, or false when they were already disabled.

--*/

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

/*++

    Routine description:

        Restores maskable interrupts to a caller-supplied state.

    Arguments:

        [IN] EnabledBefore - Whether interrupts were enabled before the matching disable operation.

    Return Values:

        None.

--*/

{
    if (EnabledBefore) __sti();
}

bool
MeAreInterruptsEnabled(
    void
)

// Short Desc: Will return if interrupts are currently enabled on the processor.

/*++

    Routine description:

        Reports whether maskable interrupts are enabled on the current processor.

    Arguments:

        None.

    Return Values:

        A nonzero value when the reported condition holds, or zero otherwise.

--*/

{
    return interrupts_enabled();
}
