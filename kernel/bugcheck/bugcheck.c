/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:		Bugcheck functions implementation.
 */

#include "bugcheck.h"
#include "../trace.h"

// We require GOP, so we extern it.
extern GOP_PARAMS gop_local;
extern LASTFUNC_HISTORY lastfunc_history;
extern bool isBugChecking;

extern uint32_t cursor_x;
extern uint32_t cursor_y;

void print_lastfunc_chain(uint32_t color) {
    // Start at the oldest entry: that's the slot `index` points to (next write).
    int idx = lastfunc_history.current_index;

    int start = (idx + 1) % LASTFUNC_HISTORY_SIZE;
    bool first = true;
    for (int i = 0; i < LASTFUNC_HISTORY_SIZE; i++) { // start from oldest to newest
        idx = (start + i) % LASTFUNC_HISTORY_SIZE;
        char* name = (char*)lastfunc_history.names[idx];
        if (!*name) break;
        if (!first) {
            gop_printf(&gop_local, color, " -> ");
        }
        gop_printf(&gop_local, color, "%s", name);
        first = false;
    }
}

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
    default:
        *s = "UNKNOWN_BUGCHECK_CODE";
        break;
    }
}

void bugcheck_system(CTX_FRAME* context, INT_FRAME* int_frame, BUGCHECK_CODES err_code, uint32_t additional, bool isAdditionals) {
    // Critical system error, instead of triple faulting, we hang the system with specified error codes.
    // Disable interrupts if they werent disabled before.
    __cli();
    isBugChecking = true;
    bool isThereIntFrame = (int_frame) ? true : false; // basic ternary
#ifdef DEBUG
    IRQL recordedIrql = cpu.currentIrql;
#endif
    // Force to be redrawn from the top, instead of last place.
    cursor_x = 0;
    cursor_y = 0;
    _SetIRQL(HIGH_LEVEL); // SET the irql to high level (not raise) (we could raise, but this takes less cycles and so is faster) (When I will integrate multi core functionality, this should SetIRQL to each cpu core.

	// Clear the screen to blue (bsod windows style)
	gop_clear_screen(&gop_local, 0xFF0035b8);
    // check if nullptr deref.
    if (err_code == PAGE_FAULT && isAdditionals && additional == 0) {
        err_code = NULL_POINTER_DEREFERENCE;
    }
	// Write some debugging and an error message
	gop_printf(&gop_local, 0xFFFFFFFF, "FATAL ERROR: Your system has encountered a fatal error.\n\n");
	gop_printf(&gop_local, 0xFFFFFFFF, "Your system has been stopped for safety.\n\n");

    char* stopCodeToStr = ""; // empty at first.
    resolveStopCode(&stopCodeToStr, err_code);

	gop_printf(&gop_local, 0xFFFFFFFF, "**STOP CODE: ");
	gop_printf(&gop_local, 0xFF8B0000, "%s", stopCodeToStr);
    gop_printf(&gop_local, 0xFF00FF00, " (numerical: %d)**", err_code);
    if (context) {
        gop_printf(&gop_local, 0xFFFFFFFF,
            "\n\nRegisters:\n\n"
            "RAX: %p RBX: %p RCX: %p RDX: %p\n\n"
            "RSI: %p RDI: %p RBP: %p RSP: %p\n\n"
            "R8 : %p R9 : %p R10: %p R11: %p \n\n"
            "R12: %p R13: %p R14: %p R15: %p\n\n\n",
            context->rax,
            context->rbx,
            context->rcx,
            context->rdx,
            context->rsi,
            context->rdi,
            context->rbp,
            context->rsp,
            context->r8,
            context->r9,
            context->r10,
            context->r11,
            context->r12,
            context->r13,
            context->r14,
            context->r15
        );
    }
	else {
        gop_printf(&gop_local, 0xFFFF0000, "\n\n\n**ERROR: NO REGISTERS.**");
	}
    // don't alert if there is no interrupt frame, the user shouldn't care and know. - i should do an IFDEF here for debug, but I could not remember that I didn't define, i'd rather keep it like this for now.
    if (isThereIntFrame) {
        gop_printf(&gop_local, (uint32_t)-1,
            "Exceptions:\n\n"
            "Vector Number: %d Error Code: %p\n\n"
            "RIP: %p CS: %p RFLAGS: %p\n",
            int_frame->vector,
            int_frame->error_code,
            int_frame->rip,
            int_frame->cs,
            int_frame->rflags
        );
    }
#ifdef DEBUG
    gop_printf(&gop_local, 0xFFFFA500, "\r\n**Last IRQL: %d**", recordedIrql);
#endif
	if (isAdditionals) {
		if (err_code == PAGE_FAULT) {
            gop_printf(&gop_local, 0xFFFFA500, "\n\n\n**FAULTY ADDRESS: %p**", additional);
		}
		else {
            gop_printf(&gop_local, 0xFFBF40BF, "\n\n\n**ADDITIONALS: %p**", additional);
		}
	}
#ifdef DEBUG
    if (lastfunc_history.names[lastfunc_history.current_index][0] != '\0') {
        gop_printf(&gop_local, 0xFFBF40BF, "\n\n**FUNCTION TRACE (oldest to newest): ");
        print_lastfunc_chain(0xFFBF40BF);
        gop_printf(&gop_local, 0xFFBF40BF, "**");
    }
#endif
	//test
	__hlt();
}