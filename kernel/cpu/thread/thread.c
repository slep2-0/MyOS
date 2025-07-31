#include "thread.h"
#include "../../bugcheck/bugcheck.h"

// lets try doxygen summaries

// Helper: remove a specific thread from a queue
static void remove_thread_from_queue(Queue* q, Thread* target) {
    Thread* prev = NULL;
    Thread* cur = q->head;
    while (cur) {
        if (cur == target) {
            // If previous, skip it over our thread.
            if (prev) {
                prev->nextThread = cur->nextThread;
            }
            // No previous but this is the target, we are in the head of the queue, make the new head skip us.
            else {
                q->head = cur->nextThread;
            }
            // We are at the end of the queue, the new end is the previous.
            if (q->tail == cur) {
                q->tail = prev;
            }
            
            // Any access to the thread nextThread will result in NULLPTR DEREFERENCE. (dangling pointer)
            target->nextThread = NULL;
            return;
        }
        
        // Advance.
        prev = cur;
        cur = cur->nextThread;
    }
}

extern Thread idleThread;
extern void restore_context(CTX_FRAME* regs);

// Clean exit for a thread—never returns!
static void ThreadExit(Thread* thread) {
    // 1) mark as dead
    thread->threadState = TERMINATED;

    // 2) unlink from ready queue (if it’s there)
    remove_thread_from_queue(&cpu.readyQueue, thread);

    // 3) zero out its TCB to wipe secrets / catch UAF
    kmemset(thread, 0, sizeof * thread);

    // 4) pick next thread
    Thread* next = dequeue(&cpu.readyQueue);
    if (!next) next = &idleThread;
    next->threadState = RUNNING;
    cpu.currentThread = next;

    // 5) jump into it—does not return
    restore_context(&next->registers);

    // should never get here
    MtBugcheck(&next->registers, NULL, THREAD_EXIT_FAILURE, 0, false);
}

static void ThreadWrapperEx(ThreadEntry thread_entry, THREAD_PARAMETER parameters, Thread* thread) {
    // thread_entry(parameters) -> void func(void*)
    thread_entry(parameters); // If thread entry takes no parameters, passing NULL is still fine.
    /// When the thread finishes execution, it will go to ThreadExit to manage cleanup.
    ThreadExit(thread);
}

void MtCreateThread(ThreadEntry entry, THREAD_PARAMETER parameters, bool kernelThread) {
    if (!kernelThread) {
        /// TODO implement user mode.
        return;
    }

    // First, allocate a new thread.
    Thread* thread = MtAllocateMemory(sizeof(Thread), _Alignof(Thread));
    if (!thread) {
        CTX_FRAME ctx;
        SAVE_CTX_FRAME(&ctx);
        MtBugcheck(&ctx, NULL, HEAP_ALLOCATION_FAILED, 0, false);
    }
    // Allocate a stackTop for the thread.
    void* stackTop = MtAllocateMemory(4096, 16);
    if (!stackTop) {
        CTX_FRAME ctx;
        SAVE_CTX_FRAME(&ctx);
        MtBugcheck(&ctx, NULL, HEAP_ALLOCATION_FAILED, 0, false);
    }
    /// For when we context switch (this is why cfm is in stackTop), set the registers to all 0. (since the stack for the thread holds the registers when we context switch)
    uint8_t* sp = (uint8_t*)stackTop;
    sp -= sizeof(CTX_FRAME);
    CTX_FRAME* cfm = (CTX_FRAME*)sp;

    kmemset(cfm, 0, sizeof * cfm); // Start with 0 in all regs.

    /// Here comes the actual part that separates between the normal and Ex version of creating the thread. - Remmber to use System V ABI as this is GCC.
    cfm->rsp = (uint64_t)sp;
    cfm->rip = (uint64_t)ThreadWrapperEx;
    cfm->rdi = (uint64_t)entry; // first argument to ThreadWrapperEx (the entry point)
    cfm->rsi = (uint64_t)parameters; // second arugment to ThreadWrapperEx (the parameter pointer)
    cfm->rdx = (uint64_t)thread; // third argument to ThreadWrapperEx, our newly created Thread ptr.

    // Set it's registers.
    thread->registers = *cfm;
    thread->threadState = READY;
    enqueue(&cpu.readyQueue, thread);
}