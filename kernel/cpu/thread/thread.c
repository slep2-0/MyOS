#include "thread.h"
#include "../../bugcheck/bugcheck.h"

// lets try doxygen summaries

// Helper: remove a specific thread from a queue
static void remove_thread_from_queue(Queue* q, Thread* target) {
    Thread* prev = NULL;
    Thread* cur = q->head;
    while (cur) {
        if (cur == target) {
            if (prev)         prev->nextThread = cur->nextThread;
            else              q->head = cur->nextThread;
            if (q->tail == cur) q->tail = prev;
            target->nextThread = NULL;
            return;
        }
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
    bugcheck_system(&next->registers, NULL, THREAD_EXIT_FAILURE, 0, false);
}

static void ThreadWrapper(void (*thread_entry)(void), Thread* thread) {
    thread_entry();
    /// When the thread finishes execution, it will go to ThreadExit to manage cleanup.
    ThreadExit(thread);
}

void CreateThread(Thread* thread, void(*entry)(void), void* stackTop, bool kernelThread) {
    if (!kernelThread) {
        thread->threadState = TERMINATED;
        return;
    }

    uint8_t* sp = (uint8_t*)stackTop;
    sp -= sizeof(CTX_FRAME);
    CTX_FRAME* cfm = (CTX_FRAME*)sp;

    kmemset(cfm, 0, sizeof * cfm); // Start with 0 in all regs.

    cfm->rsp = (uint64_t)sp;
    cfm->rip = (uint64_t)ThreadWrapper; /// We use System-V ABI, which pushes register parameters in this order: rdi, rsi, rdx, rcx, r8, and r9
    cfm->rdi = (uint64_t)entry;   // first argument to ThreadWrapper
    cfm->rsi = (uint64_t)thread;  // second argument to ThreadWrapper

    thread->registers = *cfm;
    thread->threadState = READY;
    enqueue(&cpu.readyQueue, thread);
}