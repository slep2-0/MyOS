/*++

Module Name:

    pushlock.c

Purpose:

    Small non-allocating reader/writer lock used by executive data structures.

--*/

#include "../../includes/ms.h"
#include "../../includes/me.h"
#include "../../includes/mh.h"
#include "../../intrinsics/atomic.h"
#include "../../assert.h"

void
MsAcquirePushLockExclusive(
    IN PUSH_LOCK* Lock
)
{
    assert(Lock != NULL);
    MeEnterCriticalRegion();

    while (InterlockedCompareExchangeU64(&Lock->Value, PL_LOCK_BIT, 0) != 0) {
        MhSpinAndProcessIpis();
    }
}

void
MsReleasePushLockExclusive(
    IN PUSH_LOCK* Lock
)
{
    assert(Lock != NULL);

    uint64_t Previous = InterlockedCompareExchangeU64(
        &Lock->Value,
        0,
        PL_LOCK_BIT
    );

    assert(Previous == PL_LOCK_BIT, "Exclusive push lock released without ownership.");
    MeLeaveCriticalRegion();
}

void
MsAcquirePushLockShared(
    IN PUSH_LOCK* Lock
)
{
    assert(Lock != NULL);
    MeEnterCriticalRegion();

    for (;;) {
        uint64_t Value = __atomic_load_n(&Lock->Value, ATOMIC_ORDER);

        if (Value & PL_LOCK_BIT) {
            MhSpinAndProcessIpis();
            continue;
        }

        assert((Value & (PL_WAIT_BIT | PL_WAKE_BIT)) == 0);
        assert(Value <= UINT64_MAX - PL_SHARE_INC, "Push lock shared count overflow.");

        if (InterlockedCompareExchangeU64(
            &Lock->Value,
            Value + PL_SHARE_INC,
            Value
        ) == Value) {
            return;
        }

        MhSpinAndProcessIpis();
    }
}

void
MsReleasePushLockShared(
    IN PUSH_LOCK* Lock
)
{
    assert(Lock != NULL);

    for (;;) {
        uint64_t Value = __atomic_load_n(&Lock->Value, ATOMIC_ORDER);

        assert((Value & PL_LOCK_BIT) == 0, "Shared release observed an exclusive owner.");
        assert((Value & (PL_WAIT_BIT | PL_WAKE_BIT)) == 0);
        assert(Value >= PL_SHARE_INC, "Shared push lock released without ownership.");

        if (InterlockedCompareExchangeU64(
            &Lock->Value,
            Value - PL_SHARE_INC,
            Value
        ) == Value) {
            MeLeaveCriticalRegion();
            return;
        }

        __pause();
    }
}
