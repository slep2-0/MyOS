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
    MEMORY_MAP_SIZE_OVERRUN = 0xBEEF, // The memory map has grown beyond the limit (unused).
    MANUALLY_INITIATED_CRASH = 0xBABE, // A function has manually initiated a bugcheck for testing/unknown reasons with this specific code.
    BAD_PAGING = 0xBAD, // A paging function that fails when it shouldn't.
    BLOCK_DEVICE_LIMIT_REACHED = 0x420, // 1056 - Something tried to register a block device, but the limit has been reached, bugcheck system.
    NULL_POINTER_DEREFERENCE = 0xDEAD, // Attempted dereference of a null pointer.
    FILESYSTEM_PANIC = 0xFA11, // FileSystem PANIC, usually something wrong has happened
    UNABLE_TO_INIT_TRACELASTFUNC = 0xACE, // TraceLastFunc init failed in kernel_main
    FRAME_LIMIT_REACHED = 0xBADA55, // frame limit reached when trying to allocate a physical frame.
    IRQL_NOT_LESS_OR_EQUAL = 0x1337, // Access to functions while going over the max IRQL set for them. Or lowering to higher IRQL than current IRQL.
    IRQL_NOT_GREATER_OR_EQUAL = 0x1338, // Raising IRQL to an IRQL level that is lower than the current one.
    INVALID_IRQL_SUPPLIED = 0x69420, // Invalid IRQL supplied to raising / lowering IRQL.
    NULL_CTX_RECEIVED = 0xF1FA, // A null context frame has been received to a function.
    THREAD_EXIT_FAILURE = 0x123123FF, // A thread exitted but did not schedule (somehow).
    BAD_AHCI_COUNT, // AHCI Count has went over the required limit
    AHCI_INIT_FAILED, // Initialization of AHCI has failed..
    MEMORY_LIMIT_REACHED, // The amount of physical memory has reached its maximum, allocation has failed.
    HEAP_ALLOCATION_FAILED, // Allocating from the HEAP failed for an unknown reason.
    NULL_THREAD, // A thread given to the scheduler is NULL.
    FATAL_IRQL_CORRUPTION, // IRQL Has been corrupted, somehow. Probably a buffer overflow.
    THREAD_ID_CREATION_FAILURE, // Creation of a TID (Thread ID) has failed due to reaching maximum TIDs in use by the system.
    FRAME_ALLOCATION_FAILED, // Allocating a physical frame from the frame bitmap has failed.
    FRAME_BITMAP_CREATION_FAILURE, // Creating the frame bitmap resulted in a failure.
    ASSERTION_FAILURE, // Runtime Assertion Failure (assert())
    MEMORY_INVALID_FREE,
    MEMORY_CORRUPT_HEADER,
    MEMORY_DOUBLE_FREE,
    MEMORY_CORRUPT_FOOTER,
    GUARD_PAGE_DEREFERENCE, // A guard page has been dereferenced.
} BUGCHECK_CODES;

typedef struct _GUARD_PAGE_DB {
    void* address;
    size_t pageSize;
    struct _GUARD_PAGE_DB* next;
} GUARD_PAGE_DB;

typedef struct _BUGCHECK_ADDITIONALS {
    // A String (NO NEED FOR NEWLINE CHAR)
    char str[512];
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
__attribute__((noreturn))
void MtBugcheck(CTX_FRAME* context, INT_FRAME* int_frame, BUGCHECK_CODES err_code, uint64_t additional, bool isAdditionals);

// Function to initiate bugcheck + Revised additionals
__attribute__((noreturn))
void MtBugcheckEx(CTX_FRAME* context, INT_FRAME* int_frame, BUGCHECK_CODES err_code, BUGCHECK_ADDITIONALS* additional, bool isAdditionals);

#endif
