/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:		 Full DPC Function list (for kernel ISR's)
 */

#include "../cpu.h"
extern volatile bool schedule_pending;

void ScheduleDPC(void) {
    schedule_pending = true;
}
