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

/// <summary>
/// A Timer DPC Handler, that when the DPC is reached here, will set the reschedule_needed flag to true IF the scheduler is enabled (PASSIVE_MODE is set.)
/// </summary>
/// <param name="VOID">No parameters.</param>
/// Return Values: NONE.
void TimerDPC(void) {
    tracelast_func("TimerDPC");
    // Don't call Schedule() directly. Just set a flag. - Also better for ISR.
    if (cpu.schedulerEnabled) {
        reschedule_needed = true;
    }
}