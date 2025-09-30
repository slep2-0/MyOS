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

#define THREAD_DEFAULT_STACK_SIZE 4096

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
///	Create a new thread with parameters. (if no parameters are supplied (NULL), it will be handled, so no worries)
/// </summary>
/// <param name="entry">The entry point address. (usually a function, I don't see any other use.)</param>
/// <param name="parameters">The pointer to the parameters (passed as a pointer, the function itself must convert it back to its original parameter variable)</param>
/// <param name="kernelThread">Specificies if the thread should be a kernel one or not. (If not, it will setup a process, idk how I would implement it, when I'll get on it) TODO</param>
void MtCreateThread(ThreadEntry entry, THREAD_PARAMETER parameter, timeSliceTicks TIMESLICE, bool kernelThread);

/// <summary>
/// This function will return the current working thread.
/// </summary>
/// <returns>Pointer to current Thread (struct)</returns>
Thread* MtGetCurrentThread(void);

/// <summary>
/// Acquired from mutex.asm --- Will save current thread's registers, and schedule it. (its RIP would be after this function call, so you are good)
/// </summary>
extern void MtSleepCurrentThread(CTX_FRAME* threadRegisters);

#endif
