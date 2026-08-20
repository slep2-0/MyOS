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

//#define MT_UP // Uncomment to define the system to run in UniProcessor mode (will NOT enable SMP, spinlocks only raise to DISPATCH, do not acquire the lock atomically)

//#define DISABLE_CACHE // Uncomment to disable CPU Caching on ALL CPUs.

//#define DISABLE_GOP // Uncomment to disable gop framebuffer prints. (gop_printf)

//#define MT_NO_PREEMPTION // Uncomment to force cooperative scheduling (yielding only, no forceful context switch), this would break the system under its current assumptions.

//#define PERFORMANCE_ANALYTICS // Uncomment to increment performance analytics global fields (like hyperspace mappings done, etc.)

#define POOL_DEBUGGING // Uncomment to define that after every pool free the pointer that is given gets set to NULL.

// Other Behavioural Macros TODO: 
// POOL_TAGGING (debug pool allocs)

#endif