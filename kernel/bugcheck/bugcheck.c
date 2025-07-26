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


void bugcheck_system(REGS* registers, BUGCHECK_CODES err_code, uint32_t additional, bool isAdditionals) {
    // Critical system error, instead of triple faulting, we hang the system with specified error codes.
    // Disable interrupts if they werent disabled before.
    __cli();
    isBugChecking = true;
#ifdef DEBUG
    IRQL recordedIrql = cpu.currentIrql;
#endif
    // Force to be redrawn from the top, instead of last place.
    cursor_x = 0;
    cursor_y = 0;
    _SetIRQL(HIGH_LEVEL); // SET the irql to high level (not raise) (we could raise, but this takes less cycles and so is faster) (When I will integrate multi core functionality, this should SetIRQL to each cpu core.

	// Clear the screen to blue (bsod windows style)
	gop_clear_screen(&gop_local, 0xFF0000FF);
    // check if nullptr deref.
    if (err_code == PAGE_FAULT && isAdditionals && additional == 0) {
        err_code = NULL_POINTER_DEREFERENCE;
    }
	// Write some debugging and an error message
	gop_printf(&gop_local, 0xFFFFFFFF, "FATAL ERROR: Your system has encountered a fatal error.\n\n");
	gop_printf(&gop_local, 0xFFFFFFFF, "Your system has been stopped for safety.\n\n");
	gop_printf(&gop_local, 0xFFFFFFFF, "STOP CODE: "); // I want the stop code to be yellow and there is no parsing for colors inline, so.
	gop_printf(&gop_local, 0xFFFFFF00, "%d\n", err_code);
    if (registers) {
        gop_printf(&gop_local, 0xFFFFFFFF,
            "\n\nRegisters:\n\n"
            "RAX: %p RBX: %p RCX: %p RDX: %p\n\n"
            "RSI: %p RDI: %p RBP: %p R8 : %p\n\n"
            "R9 : %p R10: %p R11: %p R12: %p\n\n"
            "R13: %p R14: %p R15: %p\n\n\n"
            "Exceptions:\n\n"
            "Vector Number: %d Error Code: %p\n\n"
            "RIP: %p CS: %p RFLAGS: %p\n",
            registers->rax,
            registers->rbx,
            registers->rcx,
            registers->rdx,
            registers->rsi,
            registers->rdi,
            registers->rbp,
            registers->r8,
            registers->r9,
            registers->r10,
            registers->r11,
            registers->r12,
            registers->r13,
            registers->r14,
            registers->r15,
            registers->vector,
            registers->error_code,
            registers->rip,
            registers->cs,
            registers->rflags
        );
    }
	else {
        gop_printf(&gop_local, 0xFFFF0000, "\n\n\n**ERROR: NO REGISTERS.**");
	}
#ifdef DEBUG
    gop_printf(&gop_local, 0xFFFFA500, "\r\n**Last IRQL: %d**", recordedIrql);
#endif
	if (isAdditionals) {
		if (err_code == PAGE_FAULT) {
            gop_printf(&gop_local, 0xFFFFA500, "\n\n\n**FAULTY ADDRESS: %p**", additional);
		}
		else {
            gop_printf(&gop_local, 0xFF800080, "\n\n\n**ADDITIONALS: %p**", additional);
		}
	}
#ifdef DEBUG
    if (lastfunc_history.names[lastfunc_history.current_index][0] != '\0') {
        gop_printf(&gop_local, 0xFF800080, "\n\n**FUNCTION TRACE (oldest to newest): ");
        print_lastfunc_chain(0xFF800080);
        gop_printf(&gop_local, 0xFF800080, "**");
    }
#endif
	//test
	__hlt();
}