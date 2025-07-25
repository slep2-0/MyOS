/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:		Bugcheck functions implementation.
 */

#include "bugcheck.h"

// We require GOP, so we extern it.
extern GOP_PARAMS gop_local;

void bugcheck_system(REGS* registers, BUGCHECK_CODES err_code, uint32_t additional, bool isAdditionals) {
	// Critical system error, instead of triple faulting, we hang the system with specified error codes.
	// Disable interrupts if they werent disabled before.
	__cli();

	// Clear the screen to blue (bsod windows style)
	gop_clear_screen(&gop_local, 0xFF0000FF);

	// Write some debugging and an error message
	gop_printf(&gop_local, 0xFFFFFFFF, "\nFATAL ERROR: Your system has encountered a fatal error.\n\n");
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
        gop_printf(&gop_local, 0xFFFF0000, "\n\n\nERROR: NO REGISTERS.");
	}
	if (isAdditionals) {
		if (err_code == PAGE_FAULT) {
            gop_printf(&gop_local, 0xFFFFA500, "\n\n\nFAULTY ADDRESS: %p", additional);
		}
		else {
            gop_printf(&gop_local, 0xFF800080, "\n\n\nADDITIONALS: %p", additional);
		}
	}
	//test
	__hlt();
}