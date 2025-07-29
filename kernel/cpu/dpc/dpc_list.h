/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:		 DPC Function list.
 */

#ifndef X86_DPC_FUNCS_H
#define X86_DPC_FUNCS_H

// Invoked by timer DPC to trigger scheduling (DPC is a deferred procedure call, basically stuff to do in interrupt service routines, instead of staying on such a high DIRQL)
void TimerDPC(void);

#endif