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

void CleanStacks(void* CleanArgsStruct) {
    tracelast_func("CleanStacks");
    // We must clean in order, first the stack THEN the thread.
    CleanArgs* arguments = (CleanArgs*)CleanArgsStruct;
    MtFreeVirtualMemory(arguments->stackPtr);
    MtFreeVirtualMemory(arguments->Thread);
    
    // Lastly free the allocated struct
    MtFreeVirtualMemory((void*)arguments);
}
