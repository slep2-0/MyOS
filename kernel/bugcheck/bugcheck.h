/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:		Bugcheck function headers and additionals.
 */
#ifndef X86_BUGCHECK_H
#define X86_BUGCHECK_H
 // Standard headers, required.
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "../cpu/irql/irql.h"
#include "../drivers/gop/gop.h"
#include "../trace.h"

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

typedef enum _CUSTOM_BUGCHECK_CODES {
    MEMORY_MAP_SIZE_OVERRUN = 0xBEEF,
    MANUALLY_INITIATED_CRASH = 0xBABE,
    BAD_PAGING = 0xBAD,
    BLOCK_DEVICE_LIMIT_REACHED = 0x420, // 1056 - Something tried to register a block device, but the limit has been reached, bugcheck system.
    NULL_POINTER_DEREFERENCE = 0xDEAD, // Attempted dereference of a null pointer.
    FILESYSTEM_PANIC = 0xFA11, // FileSystem PANIC, usually something wrong has happened
    UNABLE_TO_INIT_TRACELASTFUNC = 0xACE, // TraceLastFunc init failed in kernel_main
    FRAME_LIMIT_REACHED = 0xBADA55, // frame limit reached when trying to allocate a physical frame.
    IRQL_NOT_HIGHER_OR_EQUAL = 0x1337, // A task that requires higher or equal IRQL (e.g - accessing pagable memory, mutex, something on my kernel that would require that, i'll implement it)
    IRQL_NOT_LESS_OR_EQUAL = 0x1338, // required for lowering IRQL that is already higher than the current IRQL (vice versa for increasing)
    INVALID_IRQL_SUPPLIED = 0x69420,
} CUSTOM_BUGCHECK_CODES;

// Function to initiate bugcheck.
void bugcheck_system(REGS* registers, BUGCHECK_CODES err_code, uint32_t additional, bool isAdditionals);

void print_lastfunc_chain(uint32_t color);

#endif