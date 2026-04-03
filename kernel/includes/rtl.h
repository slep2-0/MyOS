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

FORCEINLINE
size_t
RtlCaptureStackFrames(
    void** Frames,
    size_t FramesToCapture,
    size_t FramesToSkip
)
{
    void** rbp;

    __asm__ volatile ("movq %%rbp, %0" : "=r"(rbp));

    if (Frames == NULL || FramesToCapture == 0 || rbp == NULL) {
        return 0;
    }

    while (FramesToSkip != 0) {
        void** next = (void**)(*rbp);

        if (next == NULL) {
            return 0;
        }

        if ((uintptr_t)next <= (uintptr_t)rbp) {
            return 0;
        }

        rbp = next;
        FramesToSkip--;
    }

    size_t captured = 0;

    while (captured < FramesToCapture) {
        void** next = (void**)(*rbp);
        void* ret = rbp[1];

        Frames[captured++] = ret;

        if (next == NULL) {
            break;
        }

        if ((uintptr_t)next <= (uintptr_t)rbp) {
            break;
        }

        if (((uintptr_t)next & 0xF) != 0) {
            break;
        }

        rbp = next;
    }

    return captured;
}

#endif