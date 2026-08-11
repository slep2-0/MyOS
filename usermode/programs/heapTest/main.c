#include <MatanelOS.h>
#include <mtnative.h>
#include <mtstatus.h>

#include "heap_test.h"

#define HEAP_TEST_ALIGNMENT          16u
#define HEAP_TEST_INITIAL_SIZE       (64u * 1024u)
#define HEAP_TEST_LARGE_SIZE         (69u * 1024u)
#define HEAP_TEST_WORKER_COUNT       4u
#define HEAP_TEST_WORKER_ITERATIONS  512u

typedef struct _HEAP_TEST_WORKER_CONTEXT {
    MT_HEAP_HANDLE Heap;
    uint32_t WorkerIndex;
    volatile MTSTATUS Status;
} HEAP_TEST_WORKER_CONTEXT, *PHEAP_TEST_WORKER_CONTEXT;

static NORETURN void
HeapTestFail(
    IN MTSTATUS Status
)
{
    (void)MtTerminateProcess(MtCurrentProcess(), Status);
    for (;;) {
        __asm__ volatile ("pause");
    }
}

static bool
HeapTestIsAligned(
    IN const void* Address
)
{
    return ((uintptr_t)Address & (HEAP_TEST_ALIGNMENT - 1u)) == 0;
}

static bool
HeapTestIsZeroed(
    IN const void* Address,
    IN size_t Size
)
{
    const uint8_t* Bytes = (const uint8_t*)Address;
    for (size_t Index = 0; Index < Size; Index++) {
        if (Bytes[Index] != 0) {
            return false;
        }
    }

    return true;
}

static void
HeapTestSlabs(
    IN MT_HEAP_HANDLE Heap
)
{
    static const size_t Sizes[] = {
        1, 16, 17, 31, 32, 63, 64, 127, 128, 255,
        256, 511, 512, 1023, 1024, 2047, 2048, 4095, 4096
    };

    for (size_t Index = 0; Index < sizeof(Sizes) / sizeof(Sizes[0]); Index++) {
        size_t Size = Sizes[Index];
        uint8_t* Allocation = (uint8_t*)HeapAlloc(
            Heap,
            HEAP_ALLOCATE_ZERO_MEMORY,
            Size
        );
        if (!Allocation) {
            HeapTestFail(MT_HEAP_TEST_SLAB_ALLOCATION);
        }
        if (!HeapTestIsAligned(Allocation)) {
            HeapTestFail(MT_HEAP_TEST_SLAB_ALIGNMENT);
        }
        if (!HeapTestIsZeroed(Allocation, Size)) {
            HeapTestFail(MT_HEAP_TEST_SLAB_ZEROING);
        }

        memset(Allocation, (int)(0x31u + Index), Size);
        if (!HeapFree(Heap, HEAP_FREE_NO_OPTIONS, Allocation)) {
            HeapTestFail(MT_HEAP_TEST_SLAB_FREE);
        }
    }

    void* Allocation = HeapAlloc(Heap, HEAP_ALLOCATE_NO_OPTIONS, 32);
    if (!Allocation) {
        HeapTestFail(MT_HEAP_TEST_SLAB_ALLOCATION);
    }
    if (!HeapFree(Heap, HEAP_FREE_NO_OPTIONS, Allocation)) {
        HeapTestFail(MT_HEAP_TEST_SLAB_FREE);
    }
    if (HeapFree(Heap, HEAP_FREE_NO_OPTIONS, Allocation)) {
        HeapTestFail(MT_HEAP_TEST_SLAB_DOUBLE_FREE);
    }

    void* Reused = HeapAlloc(Heap, HEAP_ALLOCATE_NO_OPTIONS, 32);
    if (Reused != Allocation) {
        HeapTestFail(MT_HEAP_TEST_SLAB_REUSE);
    }
    if (!HeapFree(Heap, HEAP_FREE_NO_OPTIONS, Reused)) {
        HeapTestFail(MT_HEAP_TEST_SLAB_FREE);
    }
}

static void
HeapTestSegments(
    IN MT_HEAP_HANDLE Heap
)
{
    static const size_t Sizes[] = { 4097, 5001, HEAP_TEST_LARGE_SIZE };

    for (size_t Index = 0; Index < sizeof(Sizes) / sizeof(Sizes[0]); Index++) {
        size_t Size = Sizes[Index];
        uint8_t* Allocation = (uint8_t*)HeapAlloc(
            Heap,
            HEAP_ALLOCATE_ZERO_MEMORY,
            Size
        );
        if (!Allocation) {
            HeapTestFail(
                Size == HEAP_TEST_LARGE_SIZE
                    ? MT_HEAP_TEST_LARGE_GROWTH
                    : MT_HEAP_TEST_SEGMENT_ALLOCATION
            );
        }
        if (!HeapTestIsAligned(Allocation)) {
            HeapTestFail(MT_HEAP_TEST_SEGMENT_ALIGNMENT);
        }
        if (!HeapTestIsZeroed(Allocation, Size)) {
            HeapTestFail(MT_HEAP_TEST_SEGMENT_ZEROING);
        }

        Allocation[0] = (uint8_t)(0x41u + Index);
        Allocation[Size - 1] = (uint8_t)(0x71u + Index);
        if (!HeapFree(Heap, HEAP_FREE_NO_OPTIONS, Allocation)) {
            HeapTestFail(MT_HEAP_TEST_SEGMENT_FREE);
        }
        if (HeapFree(Heap, HEAP_FREE_NO_OPTIONS, Allocation)) {
            HeapTestFail(MT_HEAP_TEST_INVALID_FREE);
        }
    }

    uint8_t* Allocation = (uint8_t*)HeapAlloc(
        Heap,
        HEAP_ALLOCATE_NO_OPTIONS,
        5000
    );
    if (!Allocation) {
        HeapTestFail(MT_HEAP_TEST_SEGMENT_ALLOCATION);
    }
    if (HeapFree(Heap, HEAP_FREE_NO_OPTIONS, Allocation + 16)) {
        HeapTestFail(MT_HEAP_TEST_INVALID_FREE);
    }
    if (!HeapFree(Heap, HEAP_FREE_NO_OPTIONS, Allocation)) {
        HeapTestFail(MT_HEAP_TEST_SEGMENT_FREE);
    }
}

static void
HeapTestSplitAndCoalesce(
    void
)
{
    MT_HEAP_HANDLE Heap = HeapCreate(
        HEAP_CREATE_NONE,
        HEAP_TEST_INITIAL_SIZE,
        0
    );
    if (!Heap) {
        HeapTestFail(MT_HEAP_TEST_CREATE_FAILED);
    }

    void* First = HeapAlloc(Heap, HEAP_ALLOCATE_NO_OPTIONS, 5000);
    void* Middle = HeapAlloc(Heap, HEAP_ALLOCATE_NO_OPTIONS, 7000);
    void* Last = HeapAlloc(Heap, HEAP_ALLOCATE_NO_OPTIONS, 9000);
    if (!First || !Middle || !Last) {
        HeapTestFail(MT_HEAP_TEST_SEGMENT_ALLOCATION);
    }

    if (!HeapFree(Heap, HEAP_FREE_NO_OPTIONS, Middle)) {
        HeapTestFail(MT_HEAP_TEST_SEGMENT_FREE);
    }
    void* Split = HeapAlloc(Heap, HEAP_ALLOCATE_NO_OPTIONS, 6000);
    if (Split != Middle) {
        HeapTestFail(MT_HEAP_TEST_SPLIT_REUSE);
    }
    if (!HeapFree(Heap, HEAP_FREE_NO_OPTIONS, Split) ||
        !HeapFree(Heap, HEAP_FREE_NO_OPTIONS, First) ||
        !HeapFree(Heap, HEAP_FREE_NO_OPTIONS, Last)) {
        HeapTestFail(MT_HEAP_TEST_SEGMENT_FREE);
    }

    void* Coalesced = HeapAlloc(Heap, HEAP_ALLOCATE_NO_OPTIONS, 60000);
    if (Coalesced != First) {
        HeapTestFail(MT_HEAP_TEST_COALESCING);
    }
    if (!HeapFree(Heap, HEAP_FREE_NO_OPTIONS, Coalesced)) {
        HeapTestFail(MT_HEAP_TEST_SEGMENT_FREE);
    }
    if (!HeapDestroy(Heap)) {
        HeapTestFail(MT_HEAP_TEST_DESTROY);
    }
}

static void
HeapTestMaximumSize(
    void
)
{
    MT_HEAP_HANDLE Heap = HeapCreate(HEAP_CREATE_NONE, 4096, 8192);
    if (!Heap) {
        HeapTestFail(MT_HEAP_TEST_CREATE_FAILED);
    }

    void* First = HeapAlloc(Heap, HEAP_ALLOCATE_NO_OPTIONS, 5000);
    if (!First) {
        HeapTestFail(MT_HEAP_TEST_MAXIMUM_SIZE);
    }

    void* PastMaximum = HeapAlloc(Heap, HEAP_ALLOCATE_NO_OPTIONS, 5000);
    if (PastMaximum != NULL) {
        HeapTestFail(MT_HEAP_TEST_MAXIMUM_SIZE);
    }
    if (!HeapFree(Heap, HEAP_FREE_NO_OPTIONS, First)) {
        HeapTestFail(MT_HEAP_TEST_SEGMENT_FREE);
    }
    if (!HeapDestroy(Heap)) {
        HeapTestFail(MT_HEAP_TEST_DESTROY);
    }
}

static uint32_t
HeapTestWorker(
    IN void* Parameter
)
{
    static const size_t Sizes[] = {
        24, 64, 257, 1025, 4096, 4097, 5000, HEAP_TEST_LARGE_SIZE
    };
    PHEAP_TEST_WORKER_CONTEXT Context =
        (PHEAP_TEST_WORKER_CONTEXT)Parameter;

    for (uint32_t Iteration = 0;
         Iteration < HEAP_TEST_WORKER_ITERATIONS;
         Iteration++) {
        size_t Size = Sizes[
            (Iteration + Context->WorkerIndex) /
            1u % (sizeof(Sizes) / sizeof(Sizes[0]))
        ];
        uint8_t* Allocation = (uint8_t*)HeapAlloc(
            Context->Heap,
            HEAP_ALLOCATE_ZERO_MEMORY,
            Size
        );
        if (!Allocation || !HeapTestIsAligned(Allocation) ||
            !HeapTestIsZeroed(Allocation, Size)) {
            Context->Status = MT_HEAP_TEST_CONCURRENT_ALLOCATION;
            return 1;
        }

        Allocation[0] = (uint8_t)(Context->WorkerIndex + 1u);
        Allocation[Size - 1] = (uint8_t)(Iteration + 1u);
        if (!HeapFree(Context->Heap, HEAP_FREE_NO_OPTIONS, Allocation)) {
            Context->Status = MT_HEAP_TEST_CONCURRENT_FREE;
            return 1;
        }
    }

    Context->Status = MT_SUCCESS;
    return 0;
}

static void
HeapTestConcurrency(
    void
)
{
    MT_HEAP_HANDLE Heap = HeapCreate(
        HEAP_CREATE_NONE,
        HEAP_TEST_INITIAL_SIZE,
        0
    );
    if (!Heap) {
        HeapTestFail(MT_HEAP_TEST_CREATE_FAILED);
    }

    HEAP_TEST_WORKER_CONTEXT Contexts[HEAP_TEST_WORKER_COUNT];
    HANDLE Threads[HEAP_TEST_WORKER_COUNT];
    for (uint32_t Index = 0; Index < HEAP_TEST_WORKER_COUNT; Index++) {
        Contexts[Index].Heap = Heap;
        Contexts[Index].WorkerIndex = Index;
        Contexts[Index].Status = MT_PENDING;
        Threads[Index] = CreateThread(HeapTestWorker, &Contexts[Index]);
        if (Threads[Index] == MT_INVALID_HANDLE) {
            HeapTestFail(MT_HEAP_TEST_THREAD_CREATE);
        }
    }

    for (uint32_t Index = 0; Index < HEAP_TEST_WORKER_COUNT; Index++) {
        if (WaitForSingleObject(Threads[Index], MT_INFINITE) != WAIT_OBJECT_0) {
            HeapTestFail(MT_HEAP_TEST_THREAD_WAIT);
        }
        if (Contexts[Index].Status != MT_SUCCESS) {
            HeapTestFail(Contexts[Index].Status);
        }
        if (!CloseHandle(Threads[Index])) {
            HeapTestFail(MT_HEAP_TEST_THREAD_CLOSE);
        }
    }

    if (!HeapDestroy(Heap)) {
        HeapTestFail(MT_HEAP_TEST_DESTROY);
    }
}

static void
HeapTestRepeatedDestroy(
    void
)
{
    for (uint32_t Iteration = 0; Iteration < 32; Iteration++) {
        HEAP_CREATE_OPTIONS Options = (Iteration & 1u)
            ? HEAP_NO_SERIALIZE
            : HEAP_CREATE_NONE;
        MT_HEAP_HANDLE Heap = HeapCreate(Options, 4096, 0);
        if (!Heap) {
            HeapTestFail(MT_HEAP_TEST_CREATE_FAILED);
        }

        void* Small = HeapAlloc(Heap, HEAP_ALLOCATE_NO_OPTIONS, 64);
        void* Large = HeapAlloc(Heap, HEAP_ALLOCATE_NO_OPTIONS, 8192);
        if (!Small || !Large) {
            HeapTestFail(MT_HEAP_TEST_SEGMENT_ALLOCATION);
        }

        /* HeapDestroy must reclaim live allocations with their backing heap. */
        if (!HeapDestroy(Heap)) {
            HeapTestFail(MT_HEAP_TEST_DESTROY);
        }
    }
}

static uint32_t
HeapTestSlabPhase(
    IN void* Parameter
)
{
    HeapTestSlabs((MT_HEAP_HANDLE)Parameter);
    return 0;
}

static uint32_t
HeapTestSegmentPhase(
    IN void* Parameter
)
{
    HeapTestSegments((MT_HEAP_HANDLE)Parameter);
    return 0;
}

static uint32_t
HeapTestSplitPhase(
    IN void* Parameter
)
{
    (void)Parameter;
    HeapTestSplitAndCoalesce();
    return 0;
}

static uint32_t
HeapTestMaximumPhase(
    IN void* Parameter
)
{
    (void)Parameter;
    HeapTestMaximumSize();
    return 0;
}

static uint32_t
HeapTestConcurrentPhase(
    IN void* Parameter
)
{
    (void)Parameter;
    HeapTestConcurrency();
    return 0;
}

static uint32_t
HeapTestDestroyPhase(
    IN void* Parameter
)
{
    (void)Parameter;
    HeapTestRepeatedDestroy();
    return 0;
}

static void
HeapTestRunPhase(
    IN THREAD_START_ROUTINE StartRoutine,
    IN void* Parameter,
    IN MTSTATUS UnexpectedExceptionStatus
)
{
    HANDLE Thread = CreateThread(StartRoutine, Parameter);
    if (Thread == MT_INVALID_HANDLE) {
        HeapTestFail(MT_HEAP_TEST_THREAD_CREATE);
    }
    if (WaitForSingleObject(Thread, MT_INFINITE) != WAIT_OBJECT_0) {
        HeapTestFail(MT_HEAP_TEST_THREAD_WAIT);
    }

    uint32_t ExitCode = 0;
    if (!GetExitCodeThread(Thread, &ExitCode)) {
        HeapTestFail(MT_HEAP_TEST_THREAD_WAIT);
    }
    if (!CloseHandle(Thread)) {
        HeapTestFail(MT_HEAP_TEST_THREAD_CLOSE);
    }
    if ((MTSTATUS)ExitCode != MT_SUCCESS) {
        HeapTestFail(UnexpectedExceptionStatus);
    }
}

int
main(
    void
)
{
    MT_HEAP_HANDLE ProcessHeap = GetProcessHeap();
    if (!ProcessHeap) {
        HeapTestFail(MT_HEAP_TEST_NO_PROCESS_HEAP);
    }
    if (HeapDestroy(NULL) || HeapDestroy(ProcessHeap)) {
        HeapTestFail(MT_HEAP_TEST_PROCESS_HEAP_DESTROY);
    }

    HeapTestRunPhase(
        HeapTestSlabPhase,
        ProcessHeap,
        MT_HEAP_TEST_SLAB_EXCEPTION
    );
    HeapTestRunPhase(
        HeapTestSegmentPhase,
        ProcessHeap,
        MT_HEAP_TEST_SEGMENT_EXCEPTION
    );
    HeapTestRunPhase(
        HeapTestSplitPhase,
        NULL,
        MT_HEAP_TEST_SPLIT_EXCEPTION
    );
    HeapTestRunPhase(
        HeapTestMaximumPhase,
        NULL,
        MT_HEAP_TEST_MAXIMUM_EXCEPTION
    );
    HeapTestRunPhase(
        HeapTestConcurrentPhase,
        NULL,
        MT_HEAP_TEST_CONCURRENT_EXCEPTION
    );
    HeapTestRunPhase(
        HeapTestDestroyPhase,
        NULL,
        MT_HEAP_TEST_DESTROY_EXCEPTION
    );
    return 0;
}
