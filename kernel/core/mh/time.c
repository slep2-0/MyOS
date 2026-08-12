#include <stdint.h>
#include "../../intrinsics/intrin.h"
#include "../../includes/mh.h"
#include "../../includes/me.h"

static volatile uint64_t MhTscTicksPerMillisecond;

uint64_t
MhReadTsc(
    void
)

/*++

    Routine description:

        Reads the processor time-stamp counter.

    Arguments:

        None.

    Return Values:

        The current time-stamp-counter value.

--*/

{
    // Keep surrounding MMIO/mailbox observations ordered with the timestamp.
    __asm__ volatile("lfence" ::: "memory");
    return __rdtsc();
}

void
MhInitializeTscTimebase(
    void
)

/*++

    Routine description:

        Initializes the shared TSC timing conversion state.

    Arguments:

        None.

    Return Values:

        None.

--*/

{
    uint64_t StartTsc = MhReadTsc();
    pit_sleep_ms(100);
    uint64_t EndTsc = MhReadTsc();
    uint64_t Delta = EndTsc - StartTsc;

    // Round up so a deadline cannot expire earlier than requested.
    uint64_t TicksPerMillisecond = Delta / 100ULL;
    if (Delta % 100ULL) {
        TicksPerMillisecond++;
    }

    if (Delta == 0 || TicksPerMillisecond == 0) {
        MeBugCheckEx(
            TIMEBASE_INITIALIZATION_FAILURE,
            (void*)(uintptr_t)StartTsc,
            (void*)(uintptr_t)EndTsc,
            (void*)(uintptr_t)Delta,
            NULL
        );
    }

    InterlockedStoreRelease(
        &MhTscTicksPerMillisecond,
        TicksPerMillisecond
    );
}

uint64_t
MhGetTscTicksPerMillisecond(
    void
)

/*++

    Routine description:

        Returns the calibrated number of TSC ticks per millisecond.

    Arguments:

        None.

    Return Values:

        The calculated count or size.

--*/

{
    return InterlockedLoadAcquire(&MhTscTicksPerMillisecond);
}

bool
MhTscTimeoutExpired(
    uint64_t StartTsc,
    uint64_t Milliseconds
)

/*++

    Routine description:

        Reports whether a TSC-based timeout interval has expired.

    Arguments:

        [IN] StartTsc - TSC value captured at the start of the bounded wait.
        [IN] Milliseconds - Delay interval in milliseconds.

    Return Values:

        A nonzero value when the reported condition holds, or zero otherwise.

--*/

{
    uint64_t TicksPerMillisecond = MhGetTscTicksPerMillisecond();

    if (TicksPerMillisecond == 0) {
        MeBugCheckEx(
            TIMEBASE_INITIALIZATION_FAILURE,
            (void*)(uintptr_t)StartTsc,
            (void*)(uintptr_t)Milliseconds,
            NULL,
            (void*)MhTscTimeoutExpired
        );
    }

    uint64_t TimeoutTicks = Milliseconds > UINT64_MAX / TicksPerMillisecond
        ? UINT64_MAX
        : Milliseconds * TicksPerMillisecond;

    return MhReadTsc() - StartTsc >= TimeoutTicks;
}
