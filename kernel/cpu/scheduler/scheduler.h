/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:		 Scheduler types and functions.
 */
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "../cpu.h"

extern CPU cpu;

#define KERNEL_CS       0x08
#define INITIAL_RFLAGS  0x202

// The ones below will be incorporated fully when user mode arrives.
#define USER_CS         0x1B 
#define USER_RFLAGS     0x246 // IF=1, IOPL=0, CPL=3

// Default timeslice for a new thread.
#define DEFAULT_TIMESLICE 1

// Initialize scheduler: sets up idle thread and enables preemption
void InitScheduler(void);

// Core schedule function; performs a context switch
void Schedule(void);

/* 
*  -- Create a new thread:
*  - `thread`: pointer to a Thread struct
*  - `entry`: function entry point (no args)
*  - `stackTop`: top of the thread's stack (stack grows downward)
*  - 'kernelThread': specifies if the thread should be a kernel one or not.
*/
void CreateThread(Thread* thread, void (*entry)(void), void* stackTop, bool kernelThread);

// Voluntarily relinquish CPU
void Yield(void);

// Invoked by timer DPC to trigger scheduling (DPC is a deferred procedure call, basically stuff to do in interrupt service routines, instead of staying on such a high DIRQL)
void TimerDPC(void);

#endif // SCHEDULER_H
