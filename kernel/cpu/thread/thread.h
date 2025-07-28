/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      Threading Types and Functions for the Scheduler.
 */

#ifndef X86_THREAD_H
#define X86_THREAD_H

#include "../cpu.h"
#include "../../memory/memory.h"
#include "../scheduler/scheduler.h"

#define THREAD_DEFAULT_STACK_SIZE 4096

#ifdef _MSC_VER
#define ALIGN_16 __declspec(align(16))
#else
#define ALIGN_16 __attribute__((aligned(16)))
#endif

 /// Declare a thread object + its aligned stack buffer.
 ///   name:        base name for both Thread and stack
 ///   stack_bytes: size of stack in bytes (must be constant)
#define DECLARE_THREAD(name, stack_bytes)               \
    static Thread name##_t;                              \
    static ALIGN_16 uint8_t name##_stack[(stack_bytes)];

/// Instantiate and enqueue a thread you previously declared:
///   name:        same base name used in DECLARE_THREAD
///   entry:       function pointer of type void(*)(void*)
///   parameter:   parameter pointer (or NULL)
///   isKernel:    bool, true = kernel thread
#define CREATE_THREAD(name, entry, parameter, isKernel)     \
    do {                                                     \
        CreateThread(&name##_t,                              \
                     entry,                    \
                     name##_stack + sizeof(name##_stack),    \
                     (isKernel));                            \
    } while (0)
 /*
 *  -- Create a new thread:
 *  - `thread`: pointer to a Thread struct
 *  - `entry`: function entry point (no args)
 *  - `stackTop`: top of the thread's stack (stack grows downward)
 *  - 'kernelThread': specifies if the thread should be a kernel one or not.
 */
void CreateThread(Thread* thread, void(*entry)(void), void* stackTop, bool kernelThread);

#endif