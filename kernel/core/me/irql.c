/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      IRQL Implementation (Fixed with Dispatch Level scheduling toggle)
 */

#include "../../includes/me.h"
#include "../../intrinsics/atomic.h"
#include "../../intrinsics/intrin.h"
#include <stdatomic.h>

// PRIVATE API

 // Taken from ReactOS, supports up to 1 IOAPIC.
 // Instead of masking PICs, we now use the CR8 register (or in 32bit, the MMIO of LAPIC + 0x80), to use the TPR
 // The TPR (Task Priority Register), is a special register in the Local APIC that determines which interrupts are allowed to be delivered to the CPU.
 // We use IRQL, and that IRQL translates that to TPR Levels with the macros below.
#define IRQ2VECTOR(irq)		((irq) + 0x0) // 0 is the starting point

#define IRQL2VECTOR(irql)   (IRQ2VECTOR(PROFILE_LEVEL - (irql)))
// If its IPI_LEVEL, we use the IDT vector for the IPIs, same thing from PROFILE which is the LAPIC TIMER, and anything else (like DIRQLs), we use the IRQL2VECTOR
#define IRQL2TPR(irql)	    ((irql) >= IPI_LEVEL ? LAPIC_ACTION_VECTOR : ((irql) >= PROFILE_LEVEL ? LAPIC_INTERRUPT : ((irql) > DISPATCH_LEVEL ? IRQL2VECTOR(irql) : 0)))

static inline bool interrupts_enabled(void) {
    unsigned long flags;
    __asm__ __volatile__("pushfq; popq %0" : "=r"(flags));
    return (flags & (1UL << 9)) != 0; // IF is bit 9
}
static inline uint8_t vector_to_tpr(unsigned int vector) {
    return (uint8_t)(vector >> 4);
}
#define LAPIC_ACTION_VECTOR 0xDE
#define LAPIC_INTERRUPT 0xEF
static inline unsigned int irql_to_vector(IRQL irql) {
    if (irql >= IPI_LEVEL)       return LAPIC_ACTION_VECTOR;
    else if (irql >= PROFILE_LEVEL) return LAPIC_INTERRUPT;
    else if (irql > DISPATCH_LEVEL) return IRQ2VECTOR(irql);
    else                          return 0; // no special masking
}
static void update_apic_irqs(IRQL newLevel) {
    unsigned int vec = irql_to_vector(newLevel);
    uint8_t tpr = vector_to_tpr(vec);
    __write_cr8((unsigned long)tpr); // CR8 holds the priority threshold
}
static inline void toggle_scheduler(void) {
    // schedulerEnabled should be true only at IRQL < DISPATCH_LEVEL
    MeGetCurrentProcessor()->schedulerEnabled = (MeGetCurrentProcessor()->currentIrql < DISPATCH_LEVEL);
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

    IRQL curr = atomic_load_explicit(&MeGetCurrentProcessor()->currentIrql, memory_order_acquire);
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

    Routine description : This function lowers the current IRQL of the CPU to the specified 'NewIrql', and updates IRQL rules along with it (scheduler, APIC masks...).

    Arguments:

        [IN]    IRQL NewIrql: The new IRQL to set.

    Return Values:

        None.

--*/

{
    bool prev_if = interrupts_enabled();
    __cli();

    IRQL curr = atomic_load_explicit(&MeGetCurrentProcessor()->currentIrql, memory_order_acquire);
    if (NewIrql > curr) {
        MeBugCheck(IRQL_NOT_LESS_OR_EQUAL);
    }

    MeGetCurrentProcessor()->currentIrql = NewIrql;
    toggle_scheduler();
    update_apic_irqs(NewIrql);
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