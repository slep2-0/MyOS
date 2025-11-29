#ifndef X86_MATANEL_DEBUG_H
#define X86_MATANEL_DEBUG_H

/*++

Module Name:

	md.h

Purpose:

	This module contains the header files & prototypes required for the debugger of MatanelOS. This may include non-debugger functions as well, but those that are useful for runtime.

Author:

	slep (Matanel) 2025.

Revision History:

--*/

#include "me.h"

FORCEINLINE
void*
MdGetFunctionRipAddress(
	void
)

/*++

	Routine description : Retrieves the address of the RIP slot in the current stack frame, used for debugging a stack smashing where the return address get overwritten

	Arguments:

		None.

	Return Values:

		Address of RIP Slot in stack.

--*/

{
	void** frame = (void**)__builtin_frame_address(0); // returns RBP
	return (void*)(frame + 1);  // address of saved RIP slot
}

FORCEINLINE
void
MdDebugBreak(
	void
)

// Description: Emit a debug break instruction. (INT3)

{
	__asm__ volatile("int3");
}

MTSTATUS MdSetHardwareBreakpoint(DebugCallback CallbackFunction, void* BreakpointAddress, DEBUG_ACCESS_MODE AccessMode, DEBUG_LENGTH Length);
MTSTATUS MdClearHardwareBreakpointByIndex(int index);
MTSTATUS MdClearHardwareBreakpointByAddress(void* BreakpointAddress);
int find_available_debug_reg(void);
#endif