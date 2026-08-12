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

typedef struct _HEAP_TEST_LOCK_CONTEXT {
    MT_HEAP_HANDLE Heap;
    HANDLE StartedEvent;
    HANDLE FinishedEvent;
    volatile MTSTATUS Status;
} HEAP_TEST_LOCK_CONTEXT, *PHEAP_TEST_LOCK_CONTEXT;

static NORETURN void
HeapTestFail(
    IN MTSTATUS Status
)

/*++

    Routine description:

        Reports a fatal failure detected by the user-heap test suite.

    Arguments:

        [IN] Status - Status value associated with the operation.

    Return Values:

        None.

--*/

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

/*++

    Routine description:

        Reports whether an address satisfies the required heap alignment.

    Arguments:

        [IN] Address - Virtual address affected by the operation.

    Return Values:

        A nonzero value when the reported condition holds, or zero otherwise.

--*/

{
    return ((uintptr_t)Address & (HEAP_TEST_ALIGNMENT - 1u)) == 0;
}

static bool
HeapTestIsZeroed(
    IN const void* Address,
    IN size_t Size
)

/*++

    Routine description:

        Reports whether every byte in a region is zero.

    Arguments:

        [IN] Address - Virtual address affected by the operation.
        [IN] Size - Size of the requested region or object in bytes.

    Return Values:

        A nonzero value when the reported condition holds, or zero otherwise.

--*/

{
    const uint8_t* Bytes = (const uint8_t*)Address;
    for (size_t Index = 0; Index < Size; Index++) {
        if (Bytes[Index] != 0) {
            return false;
        }
    }

    return true;
}

static bool
HeapTestHasByteValue(
    IN const void* Address,
    IN size_t Size,
    IN uint8_t Value
)

/*++

    Routine description:

        Reports whether every byte in a region equals an expected value.

    Arguments:

        [IN] Address - Virtual address affected by the operation.
        [IN] Size - Size of the requested region or object in bytes.
        [IN] Value - Value to write or process.

    Return Values:

        A nonzero value when the reported condition holds, or zero otherwise.

--*/

{
    const uint8_t* Bytes = (const uint8_t*)Address;
    for (size_t Index = 0; Index < Size; Index++) {
        if (Bytes[Index] != Value) {
            return false;
        }
    }

    return true;
}

static size_t
HeapTestExpectedSlabSize(
    IN size_t Size
)

/*++

    Routine description:

        Returns the slab allocation size expected for a request.

    Arguments:

        [IN] Size - Size of the requested region or object in bytes.

    Return Values:

        The calculated count or size.

--*/

{
    size_t ExpectedSize = 16;
    while (ExpectedSize < Size) {
        ExpectedSize *= 2;
    }

    return ExpectedSize;
}

static void
HeapTestSlabs(
    IN MT_HEAP_HANDLE Heap
)

/*++

    Routine description:

        Tests slab allocation, alignment, zeroing, and reuse.

    Arguments:

        [IN] Heap - Heap whose metadata or allocation is being processed.

    Return Values:

        None.

--*/

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
        if (HeapSize(Heap, HEAP_SIZE_NO_OPTIONS, Allocation) !=
            HeapTestExpectedSlabSize(Size)) {
            HeapTestFail(MT_HEAP_TEST_SIZE_SLAB);
        }
        if (HeapSize(Heap, HEAP_SIZE_NO_OPTIONS, Allocation + 1) !=
            MT_HEAP_SIZE_ERROR) {
            HeapTestFail(MT_HEAP_TEST_SIZE_INVALID);
        }

        memset(Allocation, (int)(0x31u + Index), Size);
        if (!HeapFree(Heap, HEAP_FREE_NO_OPTIONS, Allocation)) {
            HeapTestFail(MT_HEAP_TEST_SLAB_FREE);
        }
        if (HeapSize(Heap, HEAP_SIZE_NO_OPTIONS, Allocation) !=
            MT_HEAP_SIZE_ERROR) {
            HeapTestFail(MT_HEAP_TEST_SIZE_AFTER_FREE);
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

/*++

    Routine description:

        Tests allocations serviced by heap segments.

    Arguments:

        [IN] Heap - Heap whose metadata or allocation is being processed.

    Return Values:

        None.

--*/

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
        size_t UsableSize = HeapSize(Heap, HEAP_SIZE_NO_OPTIONS, Allocation);
        if (UsableSize < Size ||
            (UsableSize & (HEAP_TEST_ALIGNMENT - 1u)) != 0) {
            HeapTestFail(MT_HEAP_TEST_SIZE_SEGMENT);
        }
        if (HeapSize(Heap, HEAP_SIZE_NO_OPTIONS, Allocation + 16) !=
            MT_HEAP_SIZE_ERROR) {
            HeapTestFail(MT_HEAP_TEST_SIZE_INVALID);
        }

        Allocation[0] = (uint8_t)(0x41u + Index);
        Allocation[Size - 1] = (uint8_t)(0x71u + Index);
        if (!HeapFree(Heap, HEAP_FREE_NO_OPTIONS, Allocation)) {
            HeapTestFail(MT_HEAP_TEST_SEGMENT_FREE);
        }
        if (HeapSize(Heap, HEAP_SIZE_NO_OPTIONS, Allocation) !=
            MT_HEAP_SIZE_ERROR) {
            HeapTestFail(MT_HEAP_TEST_SIZE_AFTER_FREE);
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

/*++

    Routine description:

        Tests segment-block splitting and adjacent free-block coalescing.

    Arguments:

        None.

    Return Values:

        None.

--*/

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

/*++

    Routine description:

        Tests enforcement of a heap maximum size.

    Arguments:

        None.

    Return Values:

        None.

--*/

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

static void
HeapTestGenerateExceptions(
    void
)

/*++

    Routine description:

        Tests exception-generating heap allocation failures.

    Arguments:

        None.

    Return Values:

        None.

--*/

{
    MT_HEAP_HANDLE Heap = HeapCreate(HEAP_CREATE_NONE, 4096, 8192);
    if (!Heap) {
        HeapTestFail(MT_HEAP_TEST_CREATE_FAILED);
    }

    void* Allocation = HeapAlloc(Heap, HEAP_ALLOCATE_NO_OPTIONS, 5000);
    if (!Allocation) {
        HeapTestFail(MT_HEAP_TEST_GENERATE_ALLOC);
    }

    volatile bool Caught = false;
    __try {
        (void)HeapAlloc(
            Heap,
            HEAP_ALLOCATE_GENERATE_EXCEPTIONS,
            5000
        );
        HeapTestFail(MT_HEAP_TEST_GENERATE_ALLOC);
    }
    __except (
        GetExceptionCode() == (uint32_t)MT_NO_MEMORY ?
        MT_EXCEPTION_EXECUTE_HANDLER : MT_EXCEPTION_CONTINUE_SEARCH
    ) {
        Caught = true;
    }

    if (!Caught || !HeapFree(Heap, HEAP_FREE_NO_OPTIONS, Allocation) ||
        !HeapDestroy(Heap)) {
        HeapTestFail(MT_HEAP_TEST_GENERATE_ALLOC);
    }

    Heap = HeapCreate(HEAP_GENERATE_EXCEPTIONS, 4096, 8192);
    if (!Heap) {
        HeapTestFail(MT_HEAP_TEST_CREATE_FAILED);
    }

    Allocation = HeapAlloc(Heap, HEAP_ALLOCATE_NO_OPTIONS, 5000);
    if (!Allocation) {
        HeapTestFail(MT_HEAP_TEST_GENERATE_REALLOC);
    }

    Caught = false;
    __try {
        (void)HeapReAlloc(
            Heap,
            HEAP_REALLOCATE_NO_OPTIONS,
            Allocation,
            20000
        );
        HeapTestFail(MT_HEAP_TEST_GENERATE_REALLOC);
    }
    __except (
        GetExceptionCode() == (uint32_t)MT_NO_MEMORY ?
        MT_EXCEPTION_EXECUTE_HANDLER : MT_EXCEPTION_CONTINUE_SEARCH
    ) {
        Caught = true;
    }

    if (!Caught || !HeapFree(Heap, HEAP_FREE_NO_OPTIONS, Allocation) ||
        !HeapDestroy(Heap)) {
        HeapTestFail(MT_HEAP_TEST_GENERATE_REALLOC);
    }
}

static uint32_t
HeapTestWorker(
    IN void* Parameter
)

/*++

    Routine description:

        Runs the heap test worker entry point.

    Arguments:

        [IN] Parameter - Context supplied when the worker thread was created.

    Return Values:

        Zero on success, or one when the worker detects a heap failure.

--*/

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
        size_t UsableSize = HeapSize(
            Context->Heap,
            HEAP_SIZE_NO_OPTIONS,
            Allocation
        );
        if (UsableSize == MT_HEAP_SIZE_ERROR || UsableSize < Size) {
            Context->Status = MT_HEAP_TEST_SIZE_CONCURRENT;
            return 1;
        }

        Allocation[0] = (uint8_t)(Context->WorkerIndex + 1u);
        Allocation[Size - 1] = (uint8_t)(Iteration + 1u);
        if ((Iteration & 15u) == 0) {
            size_t NewSize = Size + 17;
            uint8_t FirstByte = Allocation[0];
            uint8_t LastByte = Allocation[Size - 1];
            uint8_t* Reallocated = (uint8_t*)HeapReAlloc(
                Context->Heap,
                HEAP_REALLOCATE_NO_OPTIONS,
                Allocation,
                NewSize
            );
            if (!Reallocated || Reallocated[0] != FirstByte ||
                Reallocated[Size - 1] != LastByte) {
                Context->Status = MT_HEAP_TEST_REALLOC_CONCURRENT;
                return 1;
            }

            Allocation = Reallocated;
            Size = NewSize;
        }
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

/*++

    Routine description:

        Tests concurrent heap allocation and release.

    Arguments:

        None.

    Return Values:

        None.

--*/

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

/*++

    Routine description:

        Tests repeated heap creation and destruction.

    Arguments:

        None.

    Return Values:

        None.

--*/

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

static void
HeapTestSizeValidation(
    void
)

/*++

    Routine description:

        Tests heap allocation-size reporting and invalid-pointer handling.

    Arguments:

        None.

    Return Values:

        None.

--*/

{
    MT_HEAP_HANDLE Heap = HeapCreate(HEAP_CREATE_NONE, 4096, 0);
    if (!Heap) {
        HeapTestFail(MT_HEAP_TEST_CREATE_FAILED);
    }

    void* Allocation = HeapAlloc(Heap, HEAP_ALLOCATE_NO_OPTIONS, 64);
    if (!Allocation) {
        HeapTestFail(MT_HEAP_TEST_SLAB_ALLOCATION);
    }

    if (HeapSize(NULL, HEAP_SIZE_NO_OPTIONS, Allocation) !=
            MT_HEAP_SIZE_ERROR ||
        HeapSize(Heap, HEAP_SIZE_NO_OPTIONS, NULL) !=
            MT_HEAP_SIZE_ERROR ||
        HeapSize(
            Heap,
            (HEAP_SIZE_OPTIONS)0x80000000u,
            Allocation
        ) != MT_HEAP_SIZE_ERROR ||
        HeapSize(Heap, HEAP_SIZE_NO_SERIALIZE, Allocation) != 64) {
        HeapTestFail(MT_HEAP_TEST_SIZE_INVALID);
    }

    if (!HeapFree(Heap, HEAP_FREE_NO_OPTIONS, Allocation) ||
        !HeapDestroy(Heap)) {
        HeapTestFail(MT_HEAP_TEST_DESTROY);
    }
}

static void
HeapTestReallocation(
    void
)

/*++

    Routine description:

        Tests heap reallocation across supported allocation backends.

    Arguments:

        None.

    Return Values:

        None.

--*/

{
    MT_HEAP_HANDLE Heap = HeapCreate(
        HEAP_CREATE_NONE,
        HEAP_TEST_INITIAL_SIZE,
        0
    );
    if (!Heap) {
        HeapTestFail(MT_HEAP_TEST_CREATE_FAILED);
    }

    uint8_t* Same = (uint8_t*)HeapAlloc(
        Heap,
        HEAP_ALLOCATE_NO_OPTIONS,
        17
    );
    if (!Same) {
        HeapTestFail(MT_HEAP_TEST_SLAB_ALLOCATION);
    }
    memset(Same, 0xA5, 32);

    uint8_t* SameResult = (uint8_t*)HeapReAlloc(
        Heap,
        HEAP_REALLOCATE_NO_SERIALIZE,
        Same,
        31
    );
    if (SameResult != Same ||
        !HeapTestHasByteValue(SameResult, 32, 0xA5)) {
        HeapTestFail(MT_HEAP_TEST_REALLOC_SAME);
    }
    if (!HeapFree(Heap, HEAP_FREE_NO_OPTIONS, SameResult)) {
        HeapTestFail(MT_HEAP_TEST_SLAB_FREE);
    }

    uint8_t* Moved = (uint8_t*)HeapAlloc(
        Heap,
        HEAP_ALLOCATE_NO_OPTIONS,
        32
    );
    if (!Moved) {
        HeapTestFail(MT_HEAP_TEST_SLAB_ALLOCATION);
    }
    memset(Moved, 0x5A, 32);

    uint8_t* OldMoved = Moved;
    Moved = (uint8_t*)HeapReAlloc(
        Heap,
        HEAP_REALLOCATE_ZERO_MEMORY,
        Moved,
        100
    );
    if (!Moved || Moved == OldMoved) {
        HeapTestFail(MT_HEAP_TEST_REALLOC_MOVE);
    }
    if (!HeapTestHasByteValue(Moved, 32, 0x5A)) {
        HeapTestFail(MT_HEAP_TEST_REALLOC_COPY);
    }
    if (!HeapTestIsZeroed(Moved + 32, 100 - 32)) {
        HeapTestFail(MT_HEAP_TEST_REALLOC_ZERO);
    }
    if (HeapSize(Heap, HEAP_SIZE_NO_OPTIONS, OldMoved) !=
        MT_HEAP_SIZE_ERROR) {
        HeapTestFail(MT_HEAP_TEST_REALLOC_INVALID);
    }
    if (!HeapFree(Heap, HEAP_FREE_NO_OPTIONS, Moved)) {
        HeapTestFail(MT_HEAP_TEST_SLAB_FREE);
    }

    uint8_t* InPlace = (uint8_t*)HeapAlloc(
        Heap,
        HEAP_ALLOCATE_NO_OPTIONS,
        64
    );
    if (!InPlace) {
        HeapTestFail(MT_HEAP_TEST_SLAB_ALLOCATION);
    }
    memset(InPlace, 0x3C, 64);

    if (HeapReAlloc(
            Heap,
            HEAP_REALLOCATE_IN_PLACE_ONLY,
            InPlace,
            65
        ) != NULL ||
        HeapSize(Heap, HEAP_SIZE_NO_OPTIONS, InPlace) != 64 ||
        !HeapTestHasByteValue(InPlace, 64, 0x3C)) {
        HeapTestFail(MT_HEAP_TEST_REALLOC_IN_PLACE);
    }
    if (HeapReAlloc(
            Heap,
            HEAP_REALLOCATE_IN_PLACE_ONLY,
            InPlace,
            48
        ) != InPlace) {
        HeapTestFail(MT_HEAP_TEST_REALLOC_IN_PLACE);
    }
    if (!HeapFree(Heap, HEAP_FREE_NO_OPTIONS, InPlace)) {
        HeapTestFail(MT_HEAP_TEST_SLAB_FREE);
    }

    uint8_t* Segment = (uint8_t*)HeapAlloc(
        Heap,
        HEAP_ALLOCATE_NO_OPTIONS,
        5000
    );
    if (!Segment) {
        HeapTestFail(MT_HEAP_TEST_SEGMENT_ALLOCATION);
    }
    memset(Segment, 0xC3, 5000);

    uint8_t* OldSegment = Segment;
    Segment = (uint8_t*)HeapReAlloc(
        Heap,
        HEAP_REALLOCATE_NO_OPTIONS,
        Segment,
        9000
    );
    if (!Segment || Segment == OldSegment) {
        HeapTestFail(MT_HEAP_TEST_REALLOC_MOVE);
    }
    if (!HeapTestHasByteValue(Segment, 5000, 0xC3)) {
        HeapTestFail(MT_HEAP_TEST_REALLOC_COPY);
    }
    if (HeapReAlloc(
            Heap,
            HEAP_REALLOCATE_NO_OPTIONS,
            Segment,
            4500
        ) != Segment) {
        HeapTestFail(MT_HEAP_TEST_REALLOC_SAME);
    }
    if (!HeapFree(Heap, HEAP_FREE_NO_OPTIONS, Segment)) {
        HeapTestFail(MT_HEAP_TEST_SEGMENT_FREE);
    }

    if (HeapReAlloc(NULL, HEAP_REALLOCATE_NO_OPTIONS, InPlace, 64) != NULL ||
        HeapReAlloc(Heap, HEAP_REALLOCATE_NO_OPTIONS, NULL, 64) != NULL ||
        HeapReAlloc(Heap, HEAP_REALLOCATE_NO_OPTIONS, InPlace, 0) != NULL ||
        HeapReAlloc(
            Heap,
            (HEAP_REALLOCATION_OPTIONS)0x80000000u,
            InPlace,
            64
        ) != NULL) {
        HeapTestFail(MT_HEAP_TEST_REALLOC_INVALID);
    }

    if (!HeapDestroy(Heap)) {
        HeapTestFail(MT_HEAP_TEST_DESTROY);
    }

    Heap = HeapCreate(HEAP_CREATE_NONE, 4096, 8192);
    if (!Heap) {
        HeapTestFail(MT_HEAP_TEST_CREATE_FAILED);
    }
    uint8_t* Original = (uint8_t*)HeapAlloc(
        Heap,
        HEAP_ALLOCATE_NO_OPTIONS,
        5000
    );
    if (!Original) {
        HeapTestFail(MT_HEAP_TEST_SEGMENT_ALLOCATION);
    }
    memset(Original, 0x96, 5000);

    if (HeapReAlloc(
            Heap,
            HEAP_REALLOCATE_NO_OPTIONS,
            Original,
            20000
        ) != NULL ||
        !HeapTestHasByteValue(Original, 5000, 0x96)) {
        HeapTestFail(MT_HEAP_TEST_REALLOC_PRESERVE);
    }
    if (!HeapFree(Heap, HEAP_FREE_NO_OPTIONS, Original) ||
        !HeapDestroy(Heap)) {
        HeapTestFail(MT_HEAP_TEST_DESTROY);
    }
}

static uint32_t
HeapTestLockWorker(
    IN void* Parameter
)

/*++

    Routine description:

        Runs the heap test lock worker entry point.

    Arguments:

        [IN] Parameter - Context supplied when the worker thread was created.

    Return Values:

        Zero on success, or one when the lock worker detects a failure.

--*/

{
    PHEAP_TEST_LOCK_CONTEXT Context =
        (PHEAP_TEST_LOCK_CONTEXT)Parameter;

    if (HeapUnlock(Context->Heap)) {
        Context->Status = MT_HEAP_TEST_UNLOCK_OWNER;
        SetEvent(Context->StartedEvent);
        SetEvent(Context->FinishedEvent);
        return 1;
    }

    if (!SetEvent(Context->StartedEvent)) {
        Context->Status = MT_HEAP_TEST_LOCK_BLOCKING;
        return 1;
    }

    void* Allocation = HeapAlloc(
        Context->Heap,
        HEAP_ALLOCATE_NO_OPTIONS,
        128
    );
    if (!Allocation ||
        HeapSize(Context->Heap, HEAP_SIZE_NO_OPTIONS, Allocation) != 128 ||
        !HeapFree(Context->Heap, HEAP_FREE_NO_OPTIONS, Allocation)) {
        Context->Status = MT_HEAP_TEST_LOCK_OPERATION;
        SetEvent(Context->FinishedEvent);
        return 1;
    }

    Context->Status = MT_SUCCESS;
    SetEvent(Context->FinishedEvent);
    return 0;
}

static void
HeapTestPublicLocking(
    void
)

/*++

    Routine description:

        Tests explicit heap locking through the public API.

    Arguments:

        None.

    Return Values:

        None.

--*/

{
    MT_HEAP_HANDLE Heap = HeapCreate(
        HEAP_CREATE_NONE,
        HEAP_TEST_INITIAL_SIZE,
        0
    );
    MT_HEAP_HANDLE UnserializedHeap = HeapCreate(
        HEAP_NO_SERIALIZE,
        HEAP_TEST_INITIAL_SIZE,
        0
    );
    if (!Heap || !UnserializedHeap) {
        HeapTestFail(MT_HEAP_TEST_CREATE_FAILED);
    }

    if (HeapLock(NULL) || HeapUnlock(NULL) ||
        HeapLock(UnserializedHeap) || HeapUnlock(UnserializedHeap) ||
        HeapUnlock(Heap)) {
        HeapTestFail(MT_HEAP_TEST_LOCK_INVALID);
    }

    if (!HeapLock(Heap) || !HeapLock(Heap)) {
        HeapTestFail(MT_HEAP_TEST_LOCK_RECURSIVE);
    }

    void* Allocation = HeapAlloc(
        Heap,
        HEAP_ALLOCATE_NO_OPTIONS,
        64
    );
    if (!Allocation ||
        HeapSize(Heap, HEAP_SIZE_NO_OPTIONS, Allocation) != 64) {
        HeapTestFail(MT_HEAP_TEST_LOCK_OPERATION);
    }
    Allocation = HeapReAlloc(
        Heap,
        HEAP_REALLOCATE_NO_OPTIONS,
        Allocation,
        128
    );
    if (!Allocation ||
        !HeapFree(Heap, HEAP_FREE_NO_OPTIONS, Allocation)) {
        HeapTestFail(MT_HEAP_TEST_LOCK_OPERATION);
    }

    HEAP_TEST_LOCK_CONTEXT Context;
    Context.Heap = Heap;
    Context.StartedEvent = CreateEvent(NotificationEvent, false, NULL);
    Context.FinishedEvent = CreateEvent(NotificationEvent, false, NULL);
    Context.Status = MT_PENDING;
    if (Context.StartedEvent == MT_INVALID_HANDLE ||
        Context.FinishedEvent == MT_INVALID_HANDLE) {
        HeapTestFail(MT_HEAP_TEST_LOCK_BLOCKING);
    }

    HANDLE Thread = CreateThread(HeapTestLockWorker, &Context);
    if (Thread == MT_INVALID_HANDLE) {
        HeapTestFail(MT_HEAP_TEST_THREAD_CREATE);
    }
    if (WaitForSingleObject(Context.StartedEvent, MT_INFINITE) !=
            WAIT_OBJECT_0 ||
        WaitForSingleObject(Context.FinishedEvent, 0) != WAIT_TIMEOUT) {
        HeapTestFail(MT_HEAP_TEST_LOCK_BLOCKING);
    }

    if (!HeapUnlock(Heap) ||
        WaitForSingleObject(Context.FinishedEvent, 0) != WAIT_TIMEOUT) {
        HeapTestFail(MT_HEAP_TEST_LOCK_RECURSIVE);
    }
    if (!HeapUnlock(Heap)) {
        HeapTestFail(MT_HEAP_TEST_LOCK_RECURSIVE);
    }

    if (WaitForSingleObject(Thread, MT_INFINITE) != WAIT_OBJECT_0 ||
        WaitForSingleObject(Context.FinishedEvent, 0) != WAIT_OBJECT_0 ||
        Context.Status != MT_SUCCESS) {
        HeapTestFail(
            Context.Status == MT_PENDING
                ? MT_HEAP_TEST_LOCK_BLOCKING
                : Context.Status
        );
    }

    if (!CloseHandle(Thread) ||
        !CloseHandle(Context.StartedEvent) ||
        !CloseHandle(Context.FinishedEvent) ||
        HeapUnlock(Heap)) {
        HeapTestFail(MT_HEAP_TEST_UNLOCK_OWNER);
    }

    if (!HeapDestroy(UnserializedHeap) || !HeapDestroy(Heap)) {
        HeapTestFail(MT_HEAP_TEST_DESTROY);
    }
}

static uint32_t
HeapTestSlabPhase(
    IN void* Parameter
)

/*++

    Routine description:

        Runs the slab phase of the user-heap test suite.

    Arguments:

        [IN] Parameter - Context supplied when the worker thread was created.

    Return Values:

        Zero when the phase passes; a detected failure terminates the test before return.

--*/

{
    HeapTestSlabs((MT_HEAP_HANDLE)Parameter);
    return 0;
}

static uint32_t
HeapTestSegmentPhase(
    IN void* Parameter
)

/*++

    Routine description:

        Runs the segment phase of the user-heap test suite.

    Arguments:

        [IN] Parameter - Context supplied when the worker thread was created.

    Return Values:

        Zero when the phase passes; a detected failure terminates the test before return.

--*/

{
    HeapTestSegments((MT_HEAP_HANDLE)Parameter);
    return 0;
}

static uint32_t
HeapTestSplitPhase(
    IN void* Parameter
)

/*++

    Routine description:

        Runs the split phase of the user-heap test suite.

    Arguments:

        [IN] Parameter - Context supplied when the worker thread was created.

    Return Values:

        Zero when the phase passes; a detected failure terminates the test before return.

--*/

{
    (void)Parameter;
    HeapTestSplitAndCoalesce();
    return 0;
}

static uint32_t
HeapTestMaximumPhase(
    IN void* Parameter
)

/*++

    Routine description:

        Runs the maximum phase of the user-heap test suite.

    Arguments:

        [IN] Parameter - Context supplied when the worker thread was created.

    Return Values:

        Zero when the phase passes; a detected failure terminates the test before return.

--*/

{
    (void)Parameter;
    HeapTestMaximumSize();
    return 0;
}

static uint32_t
HeapTestConcurrentPhase(
    IN void* Parameter
)

/*++

    Routine description:

        Runs the concurrent phase of the user-heap test suite.

    Arguments:

        [IN] Parameter - Context supplied when the worker thread was created.

    Return Values:

        Zero when the phase passes; a detected failure terminates the test before return.

--*/

{
    (void)Parameter;
    HeapTestConcurrency();
    return 0;
}

static uint32_t
HeapTestGenerateExceptionsPhase(
    IN void* Parameter
)

/*++

    Routine description:

        Runs the generate exceptions phase of the user-heap test suite.

    Arguments:

        [IN] Parameter - Context supplied when the worker thread was created.

    Return Values:

        Zero when the phase passes; a detected failure terminates the test before return.

--*/

{
    (void)Parameter;
    HeapTestGenerateExceptions();
    return 0;
}

static uint32_t
HeapTestDestroyPhase(
    IN void* Parameter
)

/*++

    Routine description:

        Runs the destroy phase of the user-heap test suite.

    Arguments:

        [IN] Parameter - Context supplied when the worker thread was created.

    Return Values:

        Zero when the phase passes; a detected failure terminates the test before return.

--*/

{
    (void)Parameter;
    HeapTestRepeatedDestroy();
    return 0;
}

static uint32_t
HeapTestSizePhase(
    IN void* Parameter
)

/*++

    Routine description:

        Runs the size phase of the user-heap test suite.

    Arguments:

        [IN] Parameter - Context supplied when the worker thread was created.

    Return Values:

        The calculated count or size.

--*/

{
    (void)Parameter;
    HeapTestSizeValidation();
    return 0;
}

static uint32_t
HeapTestReallocationPhase(
    IN void* Parameter
)

/*++

    Routine description:

        Runs the reallocation phase of the user-heap test suite.

    Arguments:

        [IN] Parameter - Context supplied when the worker thread was created.

    Return Values:

        Zero when the phase passes; a detected failure terminates the test before return.

--*/

{
    (void)Parameter;
    HeapTestReallocation();
    return 0;
}

static uint32_t
HeapTestPublicLockingPhase(
    IN void* Parameter
)

/*++

    Routine description:

        Runs the public locking phase of the user-heap test suite.

    Arguments:

        [IN] Parameter - Context supplied when the worker thread was created.

    Return Values:

        Zero when the phase passes; a detected failure terminates the test before return.

--*/

{
    (void)Parameter;
    HeapTestPublicLocking();
    return 0;
}

static void
HeapTestRunPhase(
    IN THREAD_START_ROUTINE StartRoutine,
    IN void* Parameter,
    IN MTSTATUS UnexpectedExceptionStatus
)

/*++

    Routine description:

        Creates a thread for one heap-test phase and waits for its result.

    Arguments:

        [IN] StartRoutine - Entry routine executed by the created thread or test phase.
        [IN] Parameter - Context supplied when the worker thread was created.
        [IN] UnexpectedExceptionStatus - Status to report if the expected exception is not raised.

    Return Values:

        None.

--*/

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

/*++

    Routine description:

        Runs the program test scenario and returns its result to the loader.

    Arguments:

        None.

    Return Values:

        Zero on successful completion, or a nonzero program failure code.

--*/

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
        HeapTestGenerateExceptionsPhase,
        NULL,
        MT_HEAP_TEST_GENERATE_EXCEPTION
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
    HeapTestRunPhase(
        HeapTestSizePhase,
        NULL,
        MT_HEAP_TEST_SIZE_EXCEPTION
    );
    HeapTestRunPhase(
        HeapTestReallocationPhase,
        NULL,
        MT_HEAP_TEST_REALLOC_EXCEPTION
    );
    HeapTestRunPhase(
        HeapTestPublicLockingPhase,
        NULL,
        MT_HEAP_TEST_LOCK_EXCEPTION
    );
    return 0;
}
