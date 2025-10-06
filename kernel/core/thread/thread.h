/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      Threading Types and Functions for the Scheduler.
 */

#ifndef X86_THREAD_H
#define X86_THREAD_H

#include "../../cpu/cpu.h"
#include "../../core/memory/memory.h"
#include "../scheduler/scheduler.h"
#include "../../mtstatus.h"

#define USER_INITIAL_STACK_TOP 0x00007FFFFFFFFFFF

/// <summary>
/// These work the same as how windows does thread parameters, they just turn the parameter into 1 void* ptr, and the function that handles it must turn it into it's equivalent struct ptr.
/// So essentially, you convert your struct ptr you want to call with the function into a void* ptr (so THREAD_PARAMETERS*), and then use that and the function you use the thread on must convert it back.
/// </summary>
typedef void* THREAD_PARAMETER;
typedef void (*ThreadEntry)(THREAD_PARAMETER);


// Explanation as to why passing NULL to a function that takes no params works:
// Since in System V ABI, the first parameter is passed in RDI.
// And so a function that takes a parametr will use RDI, and we always pass a parameter (even NULL, it is considered.)
// But what about a function that doesn't take parameters? (e.g void func(void) - It will stay be handled.
// Since a function that takes parameters GCC compiles with it reading from RDI, but since this function doesn't, it will NEVER read from RDI in the first place - so it will never even acknowledge the parameter.
// So, basically, the function didn't know the paramter was there, no corruption, nothing, it's completetly fine.
// (since we are telling the compiler to treat the function call as it had a parameter, so it will let us compile) - void func(void) - (void*)(int))func(NULL)

/// <summary>
/// The following function creates a thread within a process in the system.
/// </summary>
/// <param name="ParentProcess">[IN] Pointer to allocated process</param>
/// <param name="outThread">[IN,OPT] Out thread pointer</param>
/// <param name="entry">[IN] Entry Point of thread (address)</param>
/// <param name="parameter">[IN,OPT] Optional Parameter to be supplied to the entry point</param>
/// <param name="TIMESLICE">[IN]Amount of ticks the thread will have in the running context (use timeSliceTicks)</param>
/// <returns>MTSTATUS Status code.</returns>
MTSTATUS MtCreateThread(PROCESS* ParentProcess, Thread** outThread, ThreadEntry entry, THREAD_PARAMETER parameter, timeSliceTicks TIMESLICE);

/// <summary>
/// The following function adds a thread to the 'SYSTEM' Process.
/// </summary>
/// <param name="entry">Entry Point of thread (address)</param>
/// <param name="parameter">Optional Parameter to be supplied to the entry point</param>
/// <param name="TIMESLICE">Amount of ticks the thread will have in the running context (use timeSliceTicks)</param>
/// <returns>MTSTATUS Status Code.</returns>
MTSTATUS MtCreateSystemThread(ThreadEntry entry, THREAD_PARAMETER parameter, timeSliceTicks TIMESLICE);

/// <summary>
/// This function will return the current working thread.
/// </summary>
/// <returns>Pointer to current Thread (struct)</returns>
Thread* MtGetCurrentThread(void);

/// <summary>
/// Acquired from mutex.asm --- Will save current thread's registers, and schedule it. (its RIP would be after this function call, so you are good)
/// </summary>
extern void MtSleepCurrentThread(TRAP_FRAME* threadRegisters);

#endif
