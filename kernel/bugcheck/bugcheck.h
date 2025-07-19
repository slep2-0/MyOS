/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:		Bugcheck function headers and additionals.
 */
#ifndef X86_BUGCHECK_H
#define X86_BUGCHECK_H
#include "../kernel.h"
#include "../bugcheck/bugcheck.h"

// Bugcheck error code enums, use same exception list from CPU.
typedef enum _BUGCHECK_CODES {
    EXCEPTIONDIVIDE_BY_ZERO,
    SINGLE_STEP,
    NON_MASKABLE_INTERRUPT,
    BREAKPOINT,
    OVERFLOW,
    BOUNDS_CHECK,
    INVALID_OPCODE,
    NO_COPROCESSOR,
    DOUBLE_FAULT,
    COPROCESSOR_SEGMENT_OVERRUN,
    INVALID_TSS,
    SEGMENT_SELECTOR_NOTPRESENT,
    STACK_SEGMENT_OVERRUN,
    GENERAL_PROTECTION_FAULT,
    PAGE_FAULT,
    RESERVED,
    FLOATING_POINT_ERROR,
    ALIGNMENT_CHECK,
    SEVERE_MACHINE_CHECK,
} BUGCHECK_CODES;

// Function to initiate bugcheck.
void bugcheck_system(REGS* registers, BUGCHECK_CODES err_code, uint32_t additional, bool isAdditionals);

#endif