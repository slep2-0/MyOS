/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:		 DPC Function list.
 */

#ifndef X86_DPC_FUNCS_H
#define X86_DPC_FUNCS_H

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
void ScheduleDPC(void);

typedef struct _CleanArgs {
	void* Thread;
	void* stackPtr;
} CleanArgs;

/// <summary>
/// CleanStacks, cleans the stack for the thread stack given.
/// </summary>
/// <param name=""></param>
void CleanStacks(void* CleanArgsStruct);

#endif
