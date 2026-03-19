/*++

Module Name:

    exception.c

Purpose:

    This translation unit contains the implementation of exception checking & handling in MatanelOS (_try _except macros)

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include "../../includes/exception.h"
#include "../../includes/ps.h"

uint64_t
ExpFindKernelModeExceptionHandler(
    uint64_t Rip
)

/*++

    Routine description:
        
        Enumerates the section provided by the linker script to find a suitable exception handler for the kernel RIP given.

    Arguments:

        [IN] uint64_t Rip - Address that caused the page fault in kernel mode.

    Return Values:

        Address of exception handler if found, else 0.

--*/

{
    PEXCEPTION_RANGE Entry = __start_ex_table;

    // We could do a binary search since this is address given, but WHO cares. 
    while (Entry < __stop_ex_table) {
        // Check if the faulting RIP is within the range.
        if (Rip >= Entry->start_addr && Rip < Entry->end_addr) {
            // Found a handler!
            return Entry->handler_addr;
        }

        // Increment entry.
        Entry++;
    }

    // No exception handler found..
    return 0;
}