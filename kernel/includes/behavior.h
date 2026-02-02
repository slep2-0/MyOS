/*++

Module Name:

	behavior.h

Purpose:

	This module contains the macros & definitions that affect the behavior of the OS by changing preprocessor directives.

Author:

	slep (Matanel) 2025.

Revision History:

--*/

#ifndef X86_MATANEL_BEHAVIOR_H
#define X86_MATANEL_BEHAVIOR_H

//#define MT_UP // Uncomment to define the system to run in UniProcessor mode (will NOT enable SMP, undefs spinlocks)

//#define DISABLE_CACHE // Uncomment to disable CPU Caching on ALL CPUs.

//#define DISABLE_GOP // Uncomment to disable gop framebuffer prints. (gop_printf)

//#define MT_NO_PREEMPTION // Uncomment to force cooperative scheduling (yielding only, no forceful context switch).

#endif