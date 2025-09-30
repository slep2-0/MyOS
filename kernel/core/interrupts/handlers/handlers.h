/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:		ISR Handlers - Handle Interrupts based on number and do something.
 */
#ifndef X86_HANDLER_FUNCTIONS_H
#define X86_HANDLER_FUNCTIONS_H
 // Standard headers, required.
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "../../../cpu/cpu.h"
#include "../../../trace.h"
#include "../../bugcheck/bugcheck.h"

// Obtained from https://wiki.osdev.org/Interrupts
#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64

// IRQs

// Initialize keyboard left shift and left control to 0.
void init_keyboard(void);
// Handle keyboard interruptions.
void keyboard_handler(void);
// Initiate timer with a specified frequency.
void init_timer(unsigned long int frequency);
// Handle Timer Interruptions.
void timer_handler(bool schedulerEnabled, CTX_FRAME* ctx, INT_FRAME* intfr);
// Handle SMP IPI Generic.
void ipi_action_handler(void);

// Exceptions:


// Handle Page Faults.
void pagefault_handler(CTX_FRAME* ctx, INT_FRAME* intfr);
// Handle Double Fault - Bugcheck.
void doublefault_handler(CTX_FRAME* ctx, INT_FRAME* intfr);
// Handle Division By Zero.
void dividebyzero_handler(CTX_FRAME* ctx, INT_FRAME* intfr);

// New added.

// Handle debugger single step exception.
void debugsinglestep_handler(CTX_FRAME* ctx, INT_FRAME* intfr);
// Handle Non Maskable Interrupt exception.
void nmi_handler(CTX_FRAME* ctx, INT_FRAME* intfr);
// Handle breakpoint exception.
void breakpoint_handler(CTX_FRAME* ctx, INT_FRAME* intfr);
// Handle overflow exception.
void overflow_handler(CTX_FRAME* ctx, INT_FRAME* intfr);
// Handle bounds check exception.
void boundscheck_handler(CTX_FRAME* ctx, INT_FRAME* intfr);
// Handle invalid opcode exception.
void invalidopcode_handler(CTX_FRAME* ctx, INT_FRAME* intfr);
// Handle no coprocessor exception.
void nocoprocessor_handler(CTX_FRAME* ctx, INT_FRAME* intfr);
// Handle coprocessor segment overrun exception.
void coprocessor_segment_overrun_handler(CTX_FRAME* ctx, INT_FRAME* intfr);
// Handle Invalid TSS Exception.
void invalidtss_handler(CTX_FRAME* ctx, INT_FRAME* intfr);
// Handle segment selector not present exception.
void segment_selector_not_present_handler(CTX_FRAME* ctx, INT_FRAME* intfr);
// Handle stack segment overrun exception.
void stack_segment_overrun_handler(CTX_FRAME* ctx, INT_FRAME* intfr);
// Handle GPF Exception. -- important exception, we use the registers and error code for it.
void gpf_handler(CTX_FRAME* ctx, INT_FRAME* intfr);
// Handle floating point error exception.
void fpu_handler(CTX_FRAME* ctx, INT_FRAME* intfr);
// Handle alignment check exception.
void alignment_check_handler(CTX_FRAME* ctx, INT_FRAME* intfr);
// Handle severe machine check exception.
void severe_machine_check_handler(CTX_FRAME* ctx, INT_FRAME* intfr);

void lapic_handler(bool schedulerEnabled, CTX_FRAME* ctx, INT_FRAME* intfr);
#endif

