/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:		 Full DPC Function list (for kernel ISR's)
 */

#include "../cpu.h"

/// <summary>
///  Boolean Flag to signify to the timer ISR handler if a reschedule is needed (e.g, the DPC hasn't reached the TimerDPC func yet..)
/// </summary>
bool reschedule_needed = false;

void TimerDPC(void) {
    tracelast_func("TimerDPC");
    // Don't call Schedule() directly. Just set a flag. - Also better for ISR.
    if (cpu.schedulerEnabled) {
        reschedule_needed = true;
    }
}