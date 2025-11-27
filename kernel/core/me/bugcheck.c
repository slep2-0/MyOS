/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     GPLv3
 * PURPOSE:		Bugcheck functions implementation.
 */

#include "../../includes/me.h"
#include "../../includes/mg.h"
#include "../../intrinsics/intrin.h"
#include "../../intrinsics/atomic.h"
#include "../../includes/mh.h"
#include "../../includes/ps.h"

#ifndef DEBUG
#define DEBUG
#endif

 // We require GOP, so we extern it.
extern GOP_PARAMS gop_local;
extern bool isBugChecking;
extern bool smpInitialized;

extern uint32_t cursor_x;
extern uint32_t cursor_y;

// switched to uint64_t and not BUGCHECK_CODES since the custom ones arent in that enum, and compiler throws an error.
static void resolveStopCode(char** s, uint64_t stopcode) {
    switch (stopcode) {
    case DIVIDE_BY_ZERO:
        *s = "DIVIDE_BY_ZERO";
        break;
    case SINGLE_STEP:
        *s = "SINGLE_STEP";
        break;
    case NON_MASKABLE_INTERRUPT:
        *s = "NON_MASKABLE_INTERRUPT";
        break;
    case BREAKPOINT:
        *s = "BREAKPOINT";
        break;
    case OVERFLOW:
        *s = "OVERFLOW";
        break;
    case BOUNDS_CHECK:
        *s = "BOUNDS_CHECK";
        break;
    case INVALID_OPCODE:
        *s = "INVALID_OPCODE";
        break;
    case NO_COPROCESSOR:
        *s = "NO_COPROCESSOR";
        break;
    case DOUBLE_FAULT:
        *s = "DOUBLE_FAULT";
        break;
    case COPROCESSOR_SEGMENT_OVERRUN:
        *s = "COPROCESSOR_SEGMENT_OVERRUN";
        break;
    case INVALID_TSS:
        *s = "INVALID_TSS";
        break;
    case SEGMENT_SELECTOR_NOTPRESENT:
        *s = "SEGMENT_SELECTOR_NOTPRESENT";
        break;
    case STACK_SEGMENT_OVERRUN:
        *s = "STACK_SEGMENT_OVERRUN";
        break;
    case GENERAL_PROTECTION_FAULT:
        *s = "GENERAL_PROTECTION_FAULT";
        break;
    case PAGE_FAULT:
        *s = "PAGE_FAULT";
        break;
    case RESERVED:
        *s = "RESERVED";
        break;
    case FLOATING_POINT_ERROR:
        *s = "FLOATING_POINT_ERROR";
        break;
    case ALIGNMENT_CHECK:
        *s = "ALIGNMENT_CHECK";
        break;
    case SEVERE_MACHINE_CHECK:
        *s = "SEVERE_MACHINE_CHECK";
        break;
    case MEMORY_MAP_SIZE_OVERRUN:
        *s = "MEMORY_MAP_SIZE_OVERRUN";
        break;
    case MANUALLY_INITIATED_CRASH:
        *s = "MANUALLY_INITIATED_CRASH";
        break;
    case BAD_PAGING:
        *s = "BAD_PAGING";
        break;
    case BLOCK_DEVICE_LIMIT_REACHED:
        *s = "BLOCK_DEVICE_LIMIT_REACHED";
        break;
    case NULL_POINTER_DEREFERENCE:
        *s = "NULL_POINTER_DEREFERENCE";
        break;
    case FILESYSTEM_PANIC:
        *s = "FILESYSTEM_PANIC";
        break;
    case UNABLE_TO_INIT_TRACELASTFUNC:
        *s = "UNABLE_TO_INIT_TRACELASTFUNC";
        break;
    case FRAME_LIMIT_REACHED:
        *s = "FRAME_LIMIT_REACHED";
        break;
    case IRQL_NOT_LESS_OR_EQUAL:
        *s = "IRQL_NOT_LESS_OR_EQUAL";
        break;
    case INVALID_IRQL_SUPPLIED:
        *s = "INVALID_IRQL_SUPPLIED";
        break;
    case NULL_CTX_RECEIVED:
        *s = "NULL_CTX_RECEIVED";
        break;
    case THREAD_EXIT_FAILURE:
        *s = "THREAD_EXIT_FAILURE";
        break;
    case BAD_AHCI_COUNT:
        *s = "BAD_AHCI_COUNT";
        break;
    case AHCI_INIT_FAILED:
        *s = "AHCI_INIT_FAILED";
        break;
    case MEMORY_LIMIT_REACHED:
        *s = "MEMORY_LIMIT_REACHED";
        break;
    case HEAP_ALLOCATION_FAILED:
        *s = "HEAP_ALLOCATION_FAILED";
        break;
    case NULL_THREAD:
        *s = "NULL_THREAD";
        break;
    case FATAL_IRQL_CORRUPTION:
        *s = "FATAL_IRQL_CORRUPTION";
        break;
    case THREAD_ID_CREATION_FAILURE:
        *s = "THREAD_ID_CREATION_FAILURE";
        break;
    case ASSERTION_FAILURE:
        *s = "ASSERTION_FAILURE";
        break;
    case FRAME_ALLOCATION_FAILED:
        *s = "FRAME_ALLOCATION_FAILED";
        break;
    case FRAME_BITMAP_CREATION_FAILURE:
        *s = "FRAME_BITMAP_CREATION_FAILURE";
        break;
    case MEMORY_INVALID_FREE:
        *s = "MEMORY_INVALID_FREE";
        break;
    case MEMORY_CORRUPT_HEADER:
        *s = "MEMORY_CORRUPT_HEADER";
        break;
    case MEMORY_DOUBLE_FREE:
        *s = "MEMORY_DOUBLE_FREE";
        break;
    case MEMORY_CORRUPT_FOOTER:
        *s = "MEMORY_CORRUPT_FOOTER";
        break;
    case GUARD_PAGE_DEREFERENCE:
        *s = "GUARD_PAGE_DEREFERENCE";
        break;
    case IRQL_NOT_GREATER_OR_EQUAL:
        *s = "IRQL_NOT_GREATER_OR_EQUAL";
        break;
    case KERNEL_STACK_OVERFLOWN:
        *s = "KERNEL_STACK_OVERFLOWN";
        break;
    case BAD_POOL_CALLER:
        *s = "BAD_POOL_CALLER";
        break;
    case KMODE_EXCEPTION_NOT_HANDLED:
        *s = "KMODE_EXCEPTION_NOT_HANDLED";
        break;
    case ATTEMPTED_SWITCH_FROM_DPC:
        *s = "ATTEMPTED_SWITCH_FROM_DPC";
        break;
    default:
        *s = "UNKNOWN_BUGCHECK_CODE";
        break;
    }
}

NORETURN
void
MeBugCheck(
	IN enum _BUGCHECK_CODES BugCheckCode
)

/*++

    Routine description : Gracefully crashes the system.

    Arguments:

        [IN]    enum _BUGCHECK_CODES BugCheckCode

    Return Values:

        None - This function does not return to caller.

--*/

{
    // Critical system error, instead of triple faulting, we hang the system with specified error codes.
    if (smpInitialized) {
        // If all other cores are online, we obviously want to stop them.
        IPI_PARAMS dummy = { 0 };
        MhSendActionToCpusAndWait(CPU_ACTION_STOP, dummy);
    }
    // Disable interrupts if they werent disabled before.
    __cli();
    // atomically set isBugChecking
    InterlockedExchangeBool(&isBugChecking, 1);
#ifdef DEBUG
    IRQL recordedIrql = MeGetCurrentProcessor()->currentIrql;
#endif
    // Force to be redrawn from the top, instead of last place.
    cursor_x = 0;
    cursor_y = 0;
    _MeSetIrql(HIGH_LEVEL); // SET the irql to high level (not raise) (we could raise, but this takes less cycles and so is faster) (previous comment was setting an IRQL to each CPU Core, instead, we send an IPI up top)

    // Clear the screen to blue (bsod windows style)
    gop_clear_screen(&gop_local, 0xFF0035b8);
    // Write some debugging and an error message
    gop_printf(0xFFFFFFFF, "FATAL ERROR: Your system has encountered a fatal error.\n\n");
    gop_printf(0xFFFFFFFF, "Your system has been stopped for safety.\n\n");
    char* stopCodeToStr = ""; // empty at first.
    resolveStopCode(&stopCodeToStr, BugCheckCode);
    gop_printf(0xFFFFFFFF, "**STOP CODE: ");
    gop_printf(0xFF8B0000, "%s", stopCodeToStr);
    gop_printf(0xFF00FF00, " (numerical: %d)**\n", BugCheckCode);
#ifdef DEBUG
    gop_printf(0xFFFFA500, "**Last IRQL: %d**\n", recordedIrql);
#endif
    if (smpInitialized) {
        gop_printf(COLOR_LIME, "Sent IPI To all CPUs to HALT.\n");
        gop_printf(COLOR_LIME, "Current Executing CPU: %d\n", MeGetCurrentProcessor()->lapic_ID);
    }
    int32_t currTid = (MeGetCurrentProcessor()->currentThread) ? PsGetCurrentThread()->TID : (uint32_t)-1;
    gop_printf(0xFFFFFF00, "Current Thread ID: %d\n", currTid);
    __cli();
    while (1) {
        __hlt();
        __pause();
    }
}


NORETURN
void 
MeBugCheckEx (
    IN enum _BUGCHECK_CODES	BugCheckCode,
    IN void* BugCheckParameter1,
    IN void* BugCheckParameter2,
    IN void* BugCheckParameter3,
    IN void* BugCheckParameter4
) 

/*++

    Routine description : Gracefully crashes the system, supplies parameters for debugging by the developer.

    Arguments:

        [IN]    enum _BUGCHECK_CODES BugCheckCode
        [IN]    void* BugCheckParameter1
        [IN]    void* BugCheckParameter2
        [IN]    void* BugCheckParameter3
        [IN]    void* BugCheckParameter4

    Return Values:

        None - This function does not return to caller.

    TODO:

        Add minidumps if the filesystem is initialized.

--*/

{
    // Critical system error, instead of triple faulting, we hang the system with specified error codes.
    // Disable interrupts if they werent disabled before.
    __cli();
    if (smpInitialized) {
        // If all other cores are online, we obviously want to stop them.
        IPI_PARAMS dummy = { 0 };
        MhSendActionToCpusAndWait(CPU_ACTION_STOP, dummy);
    }
    // atomically set isBugChecking
    InterlockedExchangeBool(&isBugChecking, 1);
#ifdef DEBUG
    IRQL recordedIrql = MeGetCurrentProcessor()->currentIrql;
#endif
    // Force to be redrawn from the top, instead of last place.
    cursor_x = 0;
    cursor_y = 0;
    _MeSetIrql(HIGH_LEVEL); // SET the irql to high level (not raise) (we could raise, but this takes less cycles and so is faster)

    // Clear the screen to blue (bsod windows style)
    gop_clear_screen(&gop_local, 0xFF0035b8);
    // Write some debugging and an error message
    gop_printf(0xFFFFFFFF, "FATAL ERROR: Your system has encountered a fatal error.\n\n");
    gop_printf(0xFFFFFFFF, "Your system has been stopped for safety.\n\n");
    char* stopCodeToStr = ""; // empty at first.
    resolveStopCode(&stopCodeToStr, BugCheckCode);
    gop_printf(0xFFFFFFFF, "**STOP CODE: ");
    gop_printf(0xFF8B0000, "%s", stopCodeToStr);
    gop_printf(0xFF00FF00, " (numerical: %d)**\n", BugCheckCode);
    {
#ifdef DEBUG
        if (BugCheckCode == ASSERTION_FAILURE) {
            // Print expression, reason, file, line.
            gop_printf(COLOR_WHITE,
                "Expression: %s\n"
                "Reason: %s\n"
                "File: %s\n"
                "Line: %d\n", // Changed to %d since assert_fail passes a decimal number and not a char ptr.
                BugCheckParameter1,
                BugCheckParameter2,
                BugCheckParameter3,
                BugCheckParameter4);
        }
        else {
#endif
            // Print parameters.
            gop_printf(COLOR_WHITE,
                "Parameter 1: (Pointer: %p | Decimal: %d | Pure Hex: %x)\n"
                "Parameter 2: (Pointer: %p | Decimal: %d | Pure Hex: %x)\n"
                "Parameter 3: (Pointer: %p | Decimal: %d | Pure Hex: %x)\n"
                "Parameter 4: (Pointer: %p | Decimal: %d | Pure Hex: %x)\n",
                BugCheckParameter1, BugCheckParameter1, BugCheckParameter1,
                BugCheckParameter2, BugCheckParameter2, BugCheckParameter2,
                BugCheckParameter3, BugCheckParameter3, BugCheckParameter3,
                BugCheckParameter4, BugCheckParameter4, BugCheckParameter4);
#ifdef DEBUG
        }
#endif
    }
#ifdef DEBUG
    gop_printf(0xFFFFA500, "**Last IRQL: %d**\n", recordedIrql);
#endif
    uint32_t currTid = (MeGetCurrentProcessor()->currentThread) ? PsGetCurrentThread()->TID : (uint32_t)-1;
    gop_printf(0xFFFFFF00, "Current Thread ID: %d\n", currTid);
    if (smpInitialized) {
        gop_printf(COLOR_LIME, "Sent IPI To all CPUs to HALT.\n");
        gop_printf(COLOR_LIME, "Current Executing CPU: %d\n", MeGetCurrentProcessor()->lapic_ID);
    }
    __cli();
    while (1) {
        __hlt();
        __pause();
    }
}
