#include "thread.h"
#include "../../bugcheck/bugcheck.h"

/*
static uint32_t CreateTID(void) {
    uint32_t newTID;

    AcquireTIDLock();

    // Get the next TID
    newTID = g_NextTID;

    // Increment by 4 to maintain alignment (Windows behavior)
    g_NextTID += 4;

    // Handle wrap-around case
    if (g_NextTID < 4) {  // Overflow occurred
        g_NextTID = 4;    // Reset to first valid TID
    }

    ReleaseTIDLock();

    return newTID;
}
*/

// Clean exit for a thread—never returns!
static void ThreadExit(Thread* thread) {
#ifdef DEBUG
    gop_printf(COLOR_RED, "Reached ThreadExit\n");
#endif
    // 1) mark as dead
    thread->threadState = TERMINATED;
    thread->timeSlice = 0;

    Schedule();

    // should never get here
    CTX_FRAME ctx;
    SAVE_CTX_FRAME(&ctx);
    MtBugcheck(&ctx, NULL, THREAD_EXIT_FAILURE, 0, false);
}

static void ThreadWrapperEx(ThreadEntry thread_entry, THREAD_PARAMETER parameter, Thread* thread) {
    // thread_entry(parameters) -> void func(void*)
    thread_entry(parameter); // If thread entry takes no parameters, passing NULL is still fine.
    /// When the thread finishes execution, it will go to ThreadExit to manage cleanup.
    ThreadExit(thread);
}

void MtCreateThread(ThreadEntry entry, THREAD_PARAMETER parameter, timeSliceTicks TIMESLICE, bool kernelThread) {
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
    sp -= 64; // Extra space so we don't overwrite stuff while context switching.

    CTX_FRAME* cfm = (CTX_FRAME*)(sp);

    kmemset(cfm, 0, sizeof * cfm); // Start with 0 in all regs.

    // Set our sig and timeslice.
    thread->magic = THREAD_SIGNATURE;
    thread->timeSlice = TIMESLICE;
    thread->origTimeSlice = TIMESLICE;

    /// Here comes the actual part that separates between the normal and Ex version of creating the thread. - Remmber to use System V ABI as this is GCC.
    cfm->rsp = (uint64_t)sp;
    cfm->rip = (uint64_t)ThreadWrapperEx;
    cfm->rdi = (uint64_t)entry; // first argument to ThreadWrapperEx (the entry point)
    cfm->rsi = (uint64_t)parameter; // second arugment to ThreadWrapperEx (the parameter pointer)
    cfm->rdx = (uint64_t)thread; // third argument to ThreadWrapperEx, our newly created Thread ptr.

    // Set it's registers.
    thread->registers = *cfm;
    thread->threadState = READY;
    enqueue(&cpu.readyQueue, thread);
}