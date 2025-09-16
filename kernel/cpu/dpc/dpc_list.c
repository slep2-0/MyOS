/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:		 Full DPC Function list (for kernel ISR's)
 */

#include "../cpu.h"

void ScheduleDPC(DPC* dpc, void* arg2, void* arg3, void* arg4) {
    UNREFERENCED_PARAMETER(dpc); UNREFERENCED_PARAMETER(arg2); UNREFERENCED_PARAMETER(arg3); UNREFERENCED_PARAMETER(arg4);
    thisCPU()->schedulePending = true;
}

void CleanStacks(DPC* dpc, void* thread, void* arg3, void* arg4) {
    UNREFERENCED_PARAMETER(dpc);  UNREFERENCED_PARAMETER(arg3); UNREFERENCED_PARAMETER(arg4);
    tracelast_func("CleanStacks");
    Thread* t = (Thread*)thread;
    // We must clean in order, first the stack THEN the thread.
    MtFreeVirtualMemory(t->startStackPtr);
    MtFreeVirtualMemory(t);
    return;
}
