#include <stdint.h>
#include "../../intrinsics/intrin.h"
#include "../../includes/mh.h"
#include "../../includes/me.h"

static volatile uint64_t MhTscTicksPerMillisecond;

uint64_t
MhReadTsc(
    void
)
{
    // Keep surrounding MMIO/mailbox observations ordered with the timestamp.
    __asm__ volatile("lfence" ::: "memory");
    return __rdtsc();
}

void
MhInitializeTscTimebase(
    void
)
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
{
    return InterlockedLoadAcquire(&MhTscTicksPerMillisecond);
}

bool
MhTscTimeoutExpired(
    uint64_t StartTsc,
    uint64_t Milliseconds
)
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
