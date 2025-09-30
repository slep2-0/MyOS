/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:		 Scheduler types and functions headers.
 */
#ifndef SCHEDULER_H
#define SCHEDULER_H

// Voluntarily relinquish CPU, alias to Schedule.
#define Yield() Schedule()

#include "../../cpu/cpu.h"
#include "../../core/memory/memory.h"

extern CPU cpu0;

// Default timeslice for a new thread.
#define DEFAULT_TIMESLICE 1

#define KERNEL_CS       0x08    // Entry 1: Kernel Code
#define KERNEL_DS       0x10    // Entry 2: Kernel Data  
#define KERNEL_SS       0x10    // Same as KERNEL_DS (data segment used for stack)
#define USER_CS         0x18    // Entry 3: User Code (for future)
#define USER_DS         0x20    // Entry 4: User Data (for future)
#define USER_SS         0x20    // Same as USER_DS (for future)
#define INITIAL_RFLAGS  0x202
#define USER_RFLAGS     0x246 // IF=1, IOPL=0, CPL=3

// Initialize scheduler: sets up idle thread and enables preemption
void InitScheduler(void);

// Core schedule function; performs a context switch
void Schedule(void);

#endif // SCHEDULER_H
