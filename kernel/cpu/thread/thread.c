#include "thread.h"
#include "../../bugcheck/bugcheck.h"
#include "../../assert.h"

#define MIN_TID           4u
#define MAX_TID           0xFFFFFFFCu
#define ALIGN_DELTA       4u
#define MAX_FREE_POOL     1024u

#define THREAD_STACK_SIZE (1024*24) // 24 KiB
#define THREAD_ALIGNMENT 16
static SPINLOCK g_tid_lock = { 0 };
///
// Call with freedTid == 0 ? allocate a new TID (returns 0 on failure)
// Call with freedTid  > 0 ? release that TID back into the pool (always returns 0)
///
static uint32_t ManageTID(uint32_t freedTid)
{
    IRQL oldIrql;
    MtAcquireSpinlock(&g_tid_lock, &oldIrql);
    static uint32_t nextTID = MIN_TID;
    static uint32_t freePool[MAX_FREE_POOL];
    static uint32_t freeCount = 0;
    uint32_t result = 0;

    if (freedTid) {
        // Release path: push into free pool if aligned & room
        if ((freedTid % ALIGN_DELTA) == 0 && freeCount < MAX_FREE_POOL) {
            freePool[freeCount++] = freedTid;
        }
    }
    else { 
        // Allocate path:
        if (freeCount > 0) {
            // Reuse most-recently freed
            result = freePool[--freeCount];
        }
        else {
            // Hand out next aligned TID
            result = nextTID;
            nextTID += ALIGN_DELTA;

            // Wrap/overflow check
            if (nextTID < ALIGN_DELTA || result > MAX_TID) {
                // Exhausted all TIDs
                result = 0;
            }
        }
    }
    MtReleaseSpinlock(&g_tid_lock, oldIrql);
    return result;
}


// Clean exit for a thread—never returns!
static void ThreadExit(Thread* thread) {
    tracelast_func("ThreadExit");
#ifdef DEBUG
    gop_printf_forced(COLOR_RED, "Reached ThreadExit\n");
#endif
    // 1) mark as dead
    thread->threadState = TERMINATED;
    thread->timeSlice = 1;
    ManageTID(thread->TID);

    // Call scheduler (don't delete the stack)
    Schedule();
    /* assertions */
#ifdef DEBUG
    bool valid = MtIsHeapAddressAllocated(thread->startStackPtr);
    assert((valid) == false, "Thread's stack hasn't been freed correctly!");
#endif
    // When the stack got freed, the scheduler was called here, and since it's freed and it atttempted to PUSH the return address to the stack, we faulted.

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

static void bp_test(void* vinfo) {
    DBG_CALLBACK_INFO* info = (DBG_CALLBACK_INFO*)vinfo;
    if (!info) return;

    gop_printf_forced(0xFFFFFF00, "(READ) HWBP: idx=%d variable addr=%p rip: %p DR6=%p\n",
        info->BreakIdx, info->Address, info->IntFrame->rip, (unsigned long long)info->Dr6);
}

void MtCreateThread(ThreadEntry entry, THREAD_PARAMETER parameter, timeSliceTicks TIMESLICE, bool kernelThread) {
    if (!kernelThread) {
        /// TODO implement user mode.
        return;
    }

    uint32_t tid = ManageTID(0);

    if (!tid) {
        CTX_FRAME ctx;
        SAVE_CTX_FRAME(&ctx);
        uint32_t RIP;
        GET_RIP(RIP);
        BUGCHECK_ADDITIONALS addt = { 0 };
        ksnprintf(addt.str, sizeof(addt.str), "Creation of new TID resulted in an error <--> MtCreateThread");
        addt.ptr = (void*)(uintptr_t)RIP;
        MtBugcheckEx(&ctx, NULL, THREAD_ID_CREATION_FAILURE, &addt, true);
    }

    IRQL oldIrql;
    MtRaiseIRQL(DISPATCH_LEVEL, &oldIrql);
    // First, allocate a new thread.
    Thread* thread = MtAllocateVirtualMemory(sizeof(Thread), _Alignof(Thread));
    if (!thread) {
        CTX_FRAME ctx;
        SAVE_CTX_FRAME(&ctx);
        MtBugcheck(&ctx, NULL, HEAP_ALLOCATION_FAILED, 0, false);
    }

    // Zero it.
    kmemset((void*)thread, 0, sizeof(Thread));
    void* stackStart = MtAllocateGuardedVirtualMemory(THREAD_STACK_SIZE, THREAD_ALIGNMENT);
    if (!stackStart) {
        CTX_FRAME ctx;
        SAVE_CTX_FRAME(&ctx);
        MtBugcheck(&ctx, NULL, HEAP_ALLOCATION_FAILED, 0, false);
    }
    thread->startStackPtr = stackStart;
    // initial stack pointer should be at the high end of the allocated region
    uint8_t* top = (uint8_t*)stackStart + THREAD_STACK_SIZE;
    top = (uint8_t*)((uintptr_t)top & ~(uintptr_t)(THREAD_ALIGNMENT - 1)); // 16-byte aligned

    // reserve red zone, then place CTX_FRAME below it
    uint8_t* sp = top;
    sp -= 64;                     // red zone (leave caller-safety area)
    sp -= sizeof(CTX_FRAME);      // space for CTX_FRAME
    CTX_FRAME* cfm = (CTX_FRAME*)sp;

    kmemset(cfm, 0, sizeof * cfm);

    // Set our timeslice.
    thread->timeSlice = TIMESLICE;
    thread->origTimeSlice = TIMESLICE;

    // saved rsp must point to the top (aligned), not sp-8
    cfm->rsp = (uint64_t)top; 
    cfm->rip = (uint64_t)ThreadWrapperEx;
    cfm->rdi = (uint64_t)entry; // first argument to ThreadWrapperEx (the entry point)
    cfm->rsi = (uint64_t)parameter; // second arugment to ThreadWrapperEx (the parameter pointer)
    cfm->rdx = (uint64_t)thread; // third argument to ThreadWrapperEx, our newly created Thread ptr.

    // Create it's RFLAGS with IF bit set to 1.
    cfm->rflags |= (1 << 9ULL);

    // Set it's registers and others.
    thread->registers = *cfm;
    thread->threadState = READY;
    thread->nextThread = NULL;
    thread->TID = tid;
    MtEnqueueThreadWithLock(&thisCPU()->readyQueue, thread);
    //MtSetHardwareBreakpoint((DebugCallback)bp_test, &thread->registers.rip, DEBUG_ACCESS_WRITE, DEBUG_LEN_8);
    // Lower IRQL.
    MtLowerIRQL(oldIrql);
}

inline Thread* MtGetCurrentThread(void) {
    return thisCPU()->currentThread;
}