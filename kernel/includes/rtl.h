#ifndef X86_MATANEL_RUNTIME_LIBRARIES_H
#define X86_MATANEL_RUNTIME_LIBRARIES_H

/*++

Module Name:

    rtl.h

Purpose:

    This module contains declarations for the runtime library (RTL), which provide core support routines used by other kernel components.

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "macros.h"
#include "annotations.h"
#include "../mtstatus.h"

FORCEINLINE
void
RtlZeroMemory(
    IN  void* Destination,
    IN  size_t Length
)
/*++

Routine Description:

    Fills a block of memory with zeros.

Arguments:

    Destination - Pointer to the memory block to zero.
    Length      - Number of bytes to zero.

Return Value:

    None.

--*/
{
    unsigned char* ptr = (unsigned char*)Destination;
    while (Length--)
        *ptr++ = 0;
}

#endif