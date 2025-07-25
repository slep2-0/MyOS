/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:		ISR Handlers - Handle Interrupts based on number and do something.
 */
#ifndef X86_HANDLER_FUNCTIONS_H
#define X86_HANDLER_FUNCTIONS_H
#include "../../kernel.h"

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
void timer_handler(void);
// Handle ATA Operations.
void ata_handler(void);


// Exceptions:


// Handle Page Faults.
void pagefault_handler(REGS* r);
// Handle Double Fault - Bugcheck.
void doublefault_handler(REGS* r);
// Handle Division By Zero.
void dividebyzero_handler(REGS* r);

// New added.

// Handle debugger single step exception.
void debugsinglestep_handler(REGS* r);
// Handle Non Maskable Interrupt exception.
void nmi_handler(REGS* r);
// Handle breakpoint exception.
void breakpoint_handler(REGS* r);
// Handle overflow exception.
void overflow_handler(REGS* r);
// Handle bounds check exception.
void boundscheck_handler(REGS* r);
// Handle invalid opcode exception.
void invalidopcode_handler(REGS* r);
// Handle no coprocessor exception.
void nocoprocessor_handler(REGS* r);
// Handle coprocessor segment overrun exception.
void coprocessor_segment_overrun_handler(REGS* r);
// Handle Invalid TSS Exception.
void invalidtss_handler(REGS* r);
// Handle segment selector not present exception.
void segment_selector_not_present_handler(REGS* r);
// Handle stack segment overrun exception.
void stack_segment_overrun_handler(REGS* r);
// Handle GPF Exception. -- important exception, we use the registers and error code for it.
void gpf_handler(REGS* registers);
// Handle floating point error exception.
void fpu_handler(REGS* r);
// Handle alignment check exception.
void alignment_check_handler(REGS* r);
// Handle severe machine check exception.
void severe_machine_check_handler(REGS* r);
#endif

