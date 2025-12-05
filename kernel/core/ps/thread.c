#include "../../includes/ps.h"
#include "../../assert.h"
#include "../../includes/mg.h"

#define MIN_TID           3u
#define MAX_TID           0xFFFFFFFCu
#define ALIGN_DELTA       3u
#define MAX_FREE_POOL     1024u

#define THREAD_STACK_SIZE (1024*24) // 24 KiB
#define THREAD_ALIGNMENT 16
static SPINLOCK g_tid_lock = { 0 };

/*returns user stack TOP(user VA) on success, 0 on failure.
out_kernel_buf receives the kernel pointer to the backing buffer for free later.*/
static uintptr_t allocate_and_map_user_stack(PEPROCESS proc, size_t stack_size, void** out_kernel_buf) {
    UNREFERENCED_PARAMETER(proc); UNREFERENCED_PARAMETER(stack_size); UNREFERENCED_PARAMETER(out_kernel_buf);
    return (uintptr_t)MT_NOT_IMPLEMENTED;
    //if (!proc || stack_size == 0) return 0;

    //size_t pages = (stack_size + VirtualPageSize - 1) / VirtualPageSize;

    ///* determine user region: leave a guard page below the base */
    //uintptr_t user_top = proc->NextStackTop;                /* top (exclusive) */
    //uintptr_t user_base = user_top - pages * VirtualPageSize;   /* base (inclusive) */
    //uintptr_t guard_page = user_base - VirtualPageSize;         /* unmapped guard page */

    ///* ensure we didn't underflow address space */
    //if (user_base < 0x100000) {
    //    return 0;
    //}

    ///* allocate kernel backing buffer (kernel virtual memory) */
    //void* kernel_buf = MtAllocateVirtualMemory(pages * VirtualPageSize, VirtualPageSize);
    //if (!kernel_buf) return 0;

    ///* Map each page from backing buffer into process PML4 */
    //for (size_t i = 0; i < pages; ++i) {
    //    void* kpage = (uint8_t*)kernel_buf + i * VirtualPageSize;
    //    uintptr_t phys = MtTranslateVirtualToPhysical(kpage);
    //    uintptr_t user_va = user_base + i * VirtualPageSize;
    //    /* map into target PML4 (proc->PageDirectoryVirtual) */
    //    MtMapPageInAddressSpace(proc->InternalProcess.PageDirectoryVirtual, (void*)user_va, phys, PAGE_PRESENT | PAGE_RW | PAGE_USER);
    //}

    ///* update bump pointer leaving the guard page for next allocation */
    //proc->NextStackTop = guard_page;

    //if (out_kernel_buf) *out_kernel_buf = kernel_buf;
    //return user_base + pages * VirtualPageSize; /* user stack TOP (RSP initial) */
}

///
// Call with freedTid == 0 ? allocate a new TID (returns 0 on failure)
// Call with freedTid  > 0 ? release that TID back into the pool (always returns 0)
///
uint32_t ManageTID(uint32_t freedTid);
uint32_t ManageTID(uint32_t freedTid)
{
    IRQL oldIrql;
    MsAcquireSpinlock(&g_tid_lock, &oldIrql);
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
    MsReleaseSpinlock(&g_tid_lock, oldIrql);
    return result;
}


// Clean exit for a thread—never returns!
static void ThreadExit(void) {
#ifdef DEBUG
    gop_printf(COLOR_RED, "Reached ThreadExit\n");
#endif
    // Terminate the thread.
    PsTerminateSystemThread();
}

static void ThreadWrapperEx(ThreadEntry thread_entry, THREAD_PARAMETER parameter) {
    // thread_entry(parameters) -> void func(void*)
    thread_entry(parameter); // If thread entry takes no parameters, passing NULL is still fine.
    /// When the thread finishes execution, it will go to ThreadExit to manage cleanup.
    ThreadExit();
}

// Internal function for thread creation
MTSTATUS PsCreateThread(PEPROCESS ParentProcess, PETHREAD* outThread, ThreadEntry entry, THREAD_PARAMETER parameter, TimeSliceTicks TIMESLICE) {
    if (!ParentProcess || !entry || !TIMESLICE) return MT_INVALID_PARAM;
    UNREFERENCED_PARAMETER(ParentProcess); UNREFERENCED_PARAMETER(outThread); UNREFERENCED_PARAMETER(entry); UNREFERENCED_PARAMETER(parameter); UNREFERENCED_PARAMETER(TIMESLICE);
    return MT_NOT_IMPLEMENTED;
    //if (!MsAcquireRundownProtection(&ParentProcess->ProcessRundown)) {
    //    // Process is being terminated, abort.
    //    return MT_PROCESS_IS_TERMINATING;
    //}
    //// Rundown acquired, safe to modify.
    //IRQL oldIrql;
    //MsAcquireSpinlock(&ParentProcess->InternalProcess.ProcessLock, &oldIrql);

    //uint32_t tid = ManageTID(0);

    //if (!tid) {
    //    MsReleaseRundownProtection(&ParentProcess->ProcessRundown);
    //    MsReleaseSpinlock(&ParentProcess->InternalProcess.ProcessLock, oldIrql);
    //    return MT_NO_RESOURCES;
    //}

    //// First, allocate a new thread.
    //PETHREAD thread = MmAllocatePoolWithTag(NonPagedPool, sizeof(ETHREAD), 'THR ');
    //if (!thread) {
    //    MsReleaseRundownProtection(&ParentProcess->ProcessRundown);
    //    MsReleaseSpinlock(&ParentProcess->InternalProcess.ProcessLock, oldIrql);
    //    return MT_NO_MEMORY;
    //}

    //// Zero it.
    //kmemset((void*)thread, 0, sizeof(ETHREAD));

    //void* krnlstckPtr = NULL;
    //uintptr_t user_rsp_top = allocate_and_map_user_stack(ParentProcess, THREAD_STACK_SIZE, &krnlstckPtr);
    //if (!user_rsp_top) {
    //    MtFreeVirtualMemory(thread);
    //    MsReleaseRundownProtection(&ParentProcess->ProcessRundown);
    //    MsReleaseSpinlock(&ParentProcess->InternalProcess.ProcessLock, oldIrql);
    //    return MT_NO_MEMORY;
    //}

    //thread->InternalThread.StackBase = krnlstckPtr;

    //// reserve red zone, then place CTX_FRAME below it (working in kernel buffer)
    //TRAP_FRAME* kcfm = &thread->InternalThread.TrapRegisters;
    //kmemset(kcfm, 0, sizeof * kcfm);

    //uint64_t user_top_aligned = (uint64_t)user_rsp_top & ~(uint64_t)(THREAD_ALIGNMENT - 1);
    //kcfm->rsp = user_top_aligned;
    //kcfm->rip = (uint64_t)entry;
    //kcfm->rdi = (uint64_t)parameter;
    //kcfm->rflags |= (1 << 9ULL);
    //kcfm->cs = USER_CS;
    //kcfm->ss = USER_SS;

    //// Set our timeslice.
    //thread->InternalThread.TimeSlice = TIMESLICE;
    //thread->InternalThread.TimeSliceAllocated = TIMESLICE;

    //// Set it's registers and others.
    //thread->InternalThread.TrapRegisters = *kcfm;
    //thread->InternalThread.ThreadState = THREAD_READY;
    //thread->nextThread = NULL;
    //thread->TID = tid;
    //thread->CurrentEvent = NULL;

    //// Now, set it's parent process properties, this is the part that separates for user mode.
    //if (!ParentProcess->MainThread) {
    //    // This is the first thread, and the main thread, of the process.
    //    ParentProcess->MainThread = thread;
    //}

    //thread->ParentProcess = ParentProcess;
    //MeEnqueueThreadWithLock(&ParentProcess->AllThreads, thread);
    //// The enqueuing of the thread into the CPU Readyqueue is only if this is not the main thread, if this IS the main thread, the process creator function, should enqueue.
    //if (ParentProcess->MainThread != thread) {
    //    // This is not the main thread, we enqueue.
    //    MeEnqueueThreadWithLock(&MeGetCurrentProcessor()->readyQueue, thread);
    //}
    //if (outThread) *outThread = thread;
    //ParentProcess->NumThreads++; // Increment the number of threads.

    //MsReleaseRundownProtection(&ParentProcess->ProcessRundown);
    //MsReleaseSpinlock(&ParentProcess->InternalProcess.ProcessLock, oldIrql);
    //return MT_SUCCESS;
}

extern EPROCESS PsInitialSystemProcess;

MTSTATUS PsCreateSystemThread(ThreadEntry entry, THREAD_PARAMETER parameter, TimeSliceTicks TIMESLICE) {
    if (!PsInitialSystemProcess.PID) return MT_NOT_FOUND; // The system process, somehow, hasn't been setupped yet.
    if (!entry || !TIMESLICE) return MT_INVALID_PARAM;

    uint32_t tid = ManageTID(0);

    if (!tid) {
        return MT_NO_RESOURCES;
    }

    // First, allocate a new thread.
    PETHREAD thread = MmAllocatePoolWithTag(NonPagedPool, sizeof(ETHREAD), 'G00N');
    if (!thread) {
        return MT_NO_MEMORY;
    }

    // Zero it.
    kmemset((void*)thread, 0, sizeof(ETHREAD));
    bool LargeStack = false;
    void* stackStart = MiCreateKernelStack(LargeStack);

    if (!stackStart) {
        // free thread
        MmFreePool(thread);
        return MT_NO_MEMORY;
    }

    uintptr_t StackTop = (uintptr_t)stackStart;

    StackTop &= ~0xF; // Align to 16 bytes (clear lower 4 bits)
    StackTop -= 8; // Decrement by 8 to keep 16-byte alignment. (after pushes)

    thread->InternalThread.StackBase = stackStart; // The stackbase must be the one gotten from MiCreateKernelStack, as freeing with StackTop will result in incorrect arithmetic, and so assertion failure.
    thread->InternalThread.IsLargeStack = LargeStack;

    TRAP_FRAME* cfm = &thread->InternalThread.TrapRegisters;
    kmemset(cfm, 0, sizeof * cfm);

    // Set our timeslice.
    thread->InternalThread.TimeSlice = TIMESLICE;
    thread->InternalThread.TimeSliceAllocated = TIMESLICE;

    // saved rsp must point to the top (aligned), not sp-8
    cfm->rsp = (uint64_t)StackTop;
    cfm->rip = (uint64_t)ThreadWrapperEx;
    cfm->rdi = (uint64_t)entry; // first argument to ThreadWrapperEx (the entry point)
    cfm->rsi = (uint64_t)parameter; // second arugment to ThreadWrapperEx (the parameter pointer)

    cfm->ss = KERNEL_SS;
    cfm->cs = KERNEL_CS;

    // Create it's RFLAGS with IF bit set to 1.
    cfm->rflags |= (1 << 9ULL);

    // Set it's registers and others.
    thread->InternalThread.TrapRegisters = *cfm;
    thread->InternalThread.ThreadState = THREAD_READY;
    thread->nextThread = NULL;
    thread->TID = tid;
    thread->CurrentEvent = NULL;

    // Process stuffz
    thread->ParentProcess = &PsInitialSystemProcess; // The parent process for the system thread, is the system process.
    MeEnqueueThreadWithLock(&MeGetCurrentProcessor()->readyQueue, thread);

    return MT_SUCCESS;
}

PETHREAD 
PsGetCurrentThread (void) {
    if (!MeGetCurrentProcessor() || !MeGetCurrentProcessor()->currentThread) return NULL;
    PITHREAD currThread = MeGetCurrentProcessor()->currentThread;
    return CONTAINING_RECORD(currThread, ETHREAD, InternalThread);
}

NORETURN
void
PsTerminateSystemThread(
    void
)

/*++

    Routine description:

        Terminates the current thread. This thread is guranteed to be a system thread.

    Arguments:

        None.

    Return Values:

        This function does not return, at all.

--*/

{
    // Before all, wait for its rundown to expire (if any).
    PETHREAD CurrentThread = PsGetCurrentThread();
    assert(CurrentThread != NULL);
    assert(CurrentThread->ParentProcess == &PsInitialSystemProcess);
    MsWaitForRundownProtectionRelease(&PsGetCurrentThread()->ThreadRundown);

    // Explicitly disable interrupts, the scheduler would switch to a thread with interrupts enabled.
    // The reason we disable the interrupts, is because if the DPC executes the instant we queue it
    // (Depth is full, or priority is higher than LOW), we would technically unmap our own stack
    // Since the DPC Vector in the IDT does NOT have an IST, and when it would return ANY PUSH instruction
    // Would page fault (or memory access on the stack generally).
    // So interrupts disabled gurantee NO DPC will run at the current execution (raising to HIGH_LEVEL would deadlock the system at MhRequestSoftwareInterrupt, waiting for APIC until dawn)
    MeDisableInterrupts();

    // Terminate its stack and thread structure.
    // Initialize the DPC. (at LOW_PRIORITY, we dont want execution after unless depth is too large)
    DPC* allocatedDPC = MmAllocatePoolWithTag(NonPagedPool, sizeof(DPC), 'PAER'); // REAP/
    assert(allocatedDPC != NULL);
    if (!allocatedDPC) {
        MeBugCheck(MANUALLY_INITIATED_CRASH); // TODO Use ReaperDPC.
    }

    CurrentThread->InternalThread.ThreadState = THREAD_ZOMBIE;
    MeInitializeDpc(allocatedDPC, CleanStacks, NULL, HIGH_PRIORITY);
    MeInsertQueueDpc(allocatedDPC, (void*)CurrentThread, (void*)allocatedDPC);

    // Schedule to another thread.
    Schedule();
}