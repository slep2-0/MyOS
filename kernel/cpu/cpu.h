/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      Core CPU Sturcture and definitions.
 */

#ifndef X86_CPU_H
#define X86_CPU_H

#ifdef __INTELLISENSE__
#define __asm__ __asm
#endif

#ifndef __INTELLISENSE__
#define SAVE_CTX_FRAME(ctx_ptr)                            \
    do {                                                   \
        __asm__ volatile (                                \
            /* push all 16 GPRs */                        \
            "push %%rax\n\t"                              \
            "push %%rbx\n\t"                              \
            "push %%rcx\n\t"                              \
            "push %%rdx\n\t"                              \
            "push %%rsi\n\t"                              \
            "push %%rdi\n\t"                              \
            "push %%rbp\n\t"                              \
            "push %%r8\n\t"                               \
            "push %%r9\n\t"                               \
            "push %%r10\n\t"                              \
            "push %%r11\n\t"                              \
            "push %%r12\n\t"                              \
            "push %%r13\n\t"                              \
            "push %%r14\n\t"                              \
            "push %%r15\n\t"                              \
                                                            \
            /* store saved regs into the CTX_FRAME */      \
            "mov %%r15,  0x00(%[c])\n\t"                   \
            "mov %%r14,  0x08(%[c])\n\t"                   \
            "mov %%r13,  0x10(%[c])\n\t"                   \
            "mov %%r12,  0x18(%[c])\n\t"                   \
            "mov %%r11,  0x20(%[c])\n\t"                   \
            "mov %%r10,  0x28(%[c])\n\t"                   \
            "mov %%r9,   0x30(%[c])\n\t"                   \
            "mov %%r8,   0x38(%[c])\n\t"                   \
            "mov %%rbp,  0x40(%[c])\n\t"                   \
            "mov %%rdi,  0x48(%[c])\n\t"                   \
            "mov %%rsi,  0x50(%[c])\n\t"                   \
            "mov %%rdx,  0x58(%[c])\n\t"                   \
            "mov %%rcx,  0x60(%[c])\n\t"                   \
            "mov %%rbx,  0x68(%[c])\n\t"                   \
            "mov %%rax,  0x70(%[c])\n\t"                   \
            /* RSP before the first push = (current RSP + 15*8) */ \
            "lea 0x78(%%rax), %%rax\n\t" /* compute offset in-place */ \
            "mov %%rax, 0x78(%[c])\n\t"                   \
                                                            \
            /* pop in reverse order */                     \
            "pop  %%r15\n\t"                               \
            "pop  %%r14\n\t"                               \
            "pop  %%r13\n\t"                               \
            "pop  %%r12\n\t"                               \
            "pop  %%r11\n\t"                               \
            "pop  %%r10\n\t"                               \
            "pop  %%r9\n\t"                                \
            "pop  %%r8\n\t"                                \
            "pop  %%rbp\n\t"                               \
            "pop  %%rdi\n\t"                               \
            "pop  %%rsi\n\t"                               \
            "pop  %%rdx\n\t"                               \
            "pop  %%rcx\n\t"                               \
            "pop  %%rbx\n\t"                               \
            "pop  %%rax\n\t"                               \
            :                                              \
            : [c] "r" (ctx_ptr)     /* loads ctx_ptr into a temp reg */ \
            : "memory"              /* we write to memory */         \
        );                                                     \
    } while (0)
#else
#define SAVE_CTX_FRAME(ctx_ptr) (void*)(0)
#endif

#define GET_RIP(RIPVAR_NOPTR) \
    __asm__ __volatile__ ("lea (%%rip), %0" : "=r" (RIPVAR_NOPTR));

// instead of including kernel.h this time which causes problems, ill include each file I need.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include "cpu_types.h"
#include "irql/irql.h"
#include "spinlock/spinlock.h"
#include "dpc/dpc.h"
#include "dpc/dpc_list.h"
#include "scheduler/scheduler.h"
#include "thread/thread.h"
#include "../mtstatus.h"

/// <summary>
/// Read the current interrupt frame.
/// </summary>
/// <param name="frame">INT_FRAME pointer.</param>
void read_interrupt_frame(INT_FRAME* frame);

#ifndef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(x) (void)(x)
#endif

/// Usage: CONTAINING_RECORD(ptr, struct, ptr_member)
/// Example: 
/// CTX_FRAME* ctxframeptr = 0x1234; // Hypothetical address of the pointer.
/// Thread* threadAssociated = CONTAINING_RECORD(ctxframeptr, Thread, ctx); // Note that ctx is the member name for CTX_FRAME in the Thread struct.
#ifndef CONTAINING_RECORD
#define CONTAINING_RECORD(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#endif

// Enqueues the thread given to the queue. (acquires spinlock)
static inline void MtEnqueueThreadWithLock(Queue* queue, Thread* thread) {
	tracelast_func("MtEnqueueThreadWithLock");
    uint64_t flags;
    MtAcquireSpinlock(&queue->lock, &flags);
	thread->nextThread = NULL;
	if (!queue->head) queue->head = thread;
	else queue->tail->nextThread = thread;
	queue->tail = thread;
    MtReleaseSpinlock(&queue->lock, flags);
}

// Dequeues the current thread from the queue, returns null if none. (acquires spinlock)
static inline Thread* MtDequeueThreadWithLock(Queue* q) {
    tracelast_func("MtDequeueThreadWithLock");
    uint64_t flags;
    MtAcquireSpinlock(&q->lock, &flags);
    if (!q->head) {
        return NULL;
    }

    Thread* t = q->head;
    q->head = t->nextThread;
    if (!q->head) {
        q->tail = NULL;
    }
    t->nextThread = NULL;
    MtReleaseSpinlock(&q->lock, flags);
    return t;
}

// Enqueues the thread given to the queue.
static inline void MtEnqueueThread(Queue* queue, Thread* thread) {
    tracelast_func("MtEnqueueThread");
    thread->nextThread = NULL;
    if (!queue->head) queue->head = thread;
    else queue->tail->nextThread = thread;
    queue->tail = thread;
}

// Dequeues the current thread from the queue, returns null if none.
static inline Thread* MtDequeueThread(Queue* q) {
    tracelast_func("MtDequeueThread");
    if (!q->head) {
        return NULL;
    }

    Thread* t = q->head;
    q->head = t->nextThread;
    if (!q->head) {
        q->tail = NULL;
    }
    t->nextThread = NULL;
    return t;
}

void InitCPU(void); // defined in kernel.c

extern CPU cpu; // Grab from KERNEL.C

#endif
