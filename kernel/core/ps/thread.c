#include "../../includes/ps.h"
#include "../../assert.h"
#include "../../includes/mg.h"
#include "../../includes/ob.h"

#define MIN_TID           3u
#define MAX_TID           0xFFFFFFFCu
#define ALIGN_DELTA       3u
#define MAX_FREE_POOL     1024u

#define THREAD_STACK_SIZE (1024*24) // 24 KiB
#define THREAD_ALIGNMENT 16
static SPINLOCK g_tid_lock = { 0 };

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
    gop_printf(COLOR_RED, "Reached ThreadExit, dereferencing object.\n");
#endif
    // Decrement count of reference.
    PsTerminateThread(PsGetCurrentThread(), MT_SUCCESS);
    // If the thread is still referenced, we just schedule, however if it is not
    // The deletion routine would activate, and we would not reach the schedule call below (instead we delete the thread and then schedule, view PsTerminateSystemThread)
    Schedule();
}

static void ThreadWrapperEx(ThreadEntry thread_entry, THREAD_PARAMETER parameter) {
    // thread_entry(parameters) -> void func(void*)
    thread_entry(parameter); // If thread entry takes no parameters, passing NULL is still fine.
    /// When the thread finishes execution, it will go to ThreadExit to manage cleanup.
    ThreadExit();
}

extern EPROCESS PsInitialSystemProcess;
extern POBJECT_TYPE PsThreadType;

MTSTATUS PsCreateSystemThread(ThreadEntry entry, THREAD_PARAMETER parameter, TimeSliceTicks TIMESLICE) {
    if (!PsInitialSystemProcess.PID) return MT_NOT_FOUND; // The system process, somehow, hasn't been setupped yet.
    if (!entry || !TIMESLICE) return MT_INVALID_PARAM;

    uint32_t tid = ManageTID(0);

    if (!tid) {
        return MT_NO_RESOURCES;
    }

    // First, allocate a new thread. (using our shiny and glossy new object manager!!!)
    PETHREAD thread = (PETHREAD)ObCreateObject(PsThreadType, sizeof(ETHREAD));
    if (!thread) {
        return MT_NO_MEMORY;
    }

    // Zero it.
    kmemset((void*)thread, 0, sizeof(ETHREAD));
    bool LargeStack = false;
    void* stackStart = MiCreateKernelStack(LargeStack);

    if (!stackStart) {
        // free thread
        ObDereferenceObject(thread);
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

void
PsTerminateThread(
    IN PETHREAD Thread,
    IN MTSTATUS ExitStatus
)

{
    // On thread terminations, we only unlink them from global list, delete stacks, and other
    // BUT WE DO NOT - Delete the ETHREAD, since thats up to the object manager to do so, we do not interfere
    // with its work.
    Thread->InternalThread.ThreadState = THREAD_TERMINATING;
    Thread->ExitStatus = ExitStatus;

    // Signal all events that the thread is waiting on to execute immediately.
    // Todo parse waitblock.

    // Dereference the thread.
    ObDereferenceObject((void*)Thread);

    // Schedule away!
    Schedule();
}

void
PsDeleteThread(
    IN void* Object
)

{
    // This function is called when the reference count for this thread has reached 0 (e.g, it is no longer in use)
    // We free everything that the thread uses.
    PETHREAD Thread = (PETHREAD)Object;
    bool IsKernelThread = PsIsKernelThread(Thread);

    // Free its stack.
    if (IsKernelThread) {
        PsDeferKernelStackDeletion(Thread->InternalThread.StackBase, Thread->InternalThread.IsLargeStack);
    }
    else {
        assert(false, "User mode threads are not supported yet.");
    }

    // When we reach here, the function returns, and the ETHREAD is deleted.
}