/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      IRQL Header for MatanelOS.
 */
#ifndef X86_IRQL_H
#define X86_IRQL_H

#include "../kernel.h"
#include "../interrupts/idt.h"
#include "../bugcheck/bugcheck.h"

// Structs & Enums


// Scheduling disabling is by flipping a global flag, I should make a CPU structure that is the current CPU states and data.
typedef enum _IRQL {
	PASSIVE_LEVEL = 1, // Passive level IRQL, normal thread execution, everything is allowed.
	DISPATCH_LEVEL = 2, // Scheduler is disabled, timers are allowed, interrupts below are masked (and memory allocations cant happen).
	DEVICE_LEVEL = 3, // DIRQL in windows - Device interrupts like keyboard, mouse, and other will be masked. Only power related stuff (when it will be implemented), or NMI (which run at HIGH_LEVEL), can interrupt.
	HIGH_LEVEL = 20, // Highest Level, this is reserved for machine checks and NMI (non maskable interrupt)
} IRQL;

// Functions
void GetCurrentIRQL(IRQL* current_irql);

void RaiseIRQL(IRQL new_irql, IRQL* old_irql);

void LowerIRQL(IRQL new_irql, IRQL* old_irql);

// Use carefully, non careful use could halt machine.
void _SetIRQL(IRQL new_irql);

void enforce_max_irql(IRQL max_allowed); 

#endif
