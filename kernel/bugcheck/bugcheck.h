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
    DIVIDE_BY_ZERO,
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
    /// Custom ones
    MEMORY_MAP_SIZE_OVERRUN = 0xBEEF,
    MANUALLY_INITIATED_CRASH = 0xBABE,
    BAD_PAGING = 0xBAD,
    BLOCK_DEVICE_LIMIT_REACHED = 0x420, // 1056 - Something tried to register a block device, but the limit has been reached, bugcheck system.
    NULL_POINTER_DEREFERENCE = 0xDEAD, // Attempted dereference of a null pointer.
    FILESYSTEM_PANIC = 0xFA11, // FileSystem PANIC, usually something wrong has happened
    UNABLE_TO_INIT_TRACELASTFUNC = 0xACE, // TraceLastFunc init failed in kernel_main
    FRAME_LIMIT_REACHED = 0xBADA55, // frame limit reached when trying to allocate a physical frame.
    IRQL_NOT_LESS_OR_EQUAL = 0x1337, // Access to functions while going over the max IRQL set for them. Or raising to lower \ lowering to higher IRQL than current IRQL.
    INVALID_IRQL_SUPPLIED = 0x69420,
    NULL_CTX_RECEIVED = 0xF1FA,
    THREAD_EXIT_FAILURE = 0x123123FF,
    BAD_AHCI_COUNT,
    AHCI_INIT_FAILED,
    MEMORY_LIMIT_REACHED,
    HEAP_ALLOCATION_FAILED,
    NULL_THREAD,
    FATAL_IRQL_CORRUPTION,
} BUGCHECK_CODES;

typedef struct _BUGCHECK_ADDITIONALS {
    // A String
    char* str;
    // A Number (UNSIGNED)
    uint64_t num;
    // A Number (SIGNED)
    int64_t signednum;
    // A Boolean
    bool boolean;
    // A general pointer.
    void* ptr;
} BUGCHECK_ADDITIONALS;

// Function to initiate bugcheck.
void MtBugcheck(CTX_FRAME* context, INT_FRAME* int_frame, BUGCHECK_CODES err_code, uint32_t additional, bool isAdditionals);

// Function to initiate bugcheck + Revised additionals
void MtBugcheckEx(CTX_FRAME* context, INT_FRAME* int_frame, BUGCHECK_CODES err_code, BUGCHECK_ADDITIONALS* additional, bool isAdditionals);

#endif