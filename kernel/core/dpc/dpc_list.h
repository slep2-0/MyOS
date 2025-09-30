/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:		 DPC Function list.
 */

#ifndef X86_DPC_FUNCS_H
#define X86_DPC_FUNCS_H

// This translation unit holds the common deferred routines (DPCs) that are used by the kernel, to keep in a centralized place.

 /// <summary>
/// Keyboard Handling DPC.
/// </summary>
/// <param name="scancode">Scancode (RDI)</param>
/// <param name="extended">Is Extended Scancode? (RSI)</param>
void keyboard_dpc(void* ctxfr);

/// <summary>
/// ScheduleDPC, used to enable the Scheduling_needed flag to TRUE.
/// </summary>
/// <param name=""></param>
void ScheduleDPC(DPC* dpc, void* arg2, void* arg3, void* arg4);

/// <summary>
/// CleanStacks, cleans the stack for the thread stack given.
/// </summary>
/// <param name=""></param>
void CleanStacks(DPC* dpc, void* thread, void* arg3, void* arg4);

#endif
