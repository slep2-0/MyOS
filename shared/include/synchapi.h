/*
 * PROJECT:      MatanelOS
 * LICENSE:      GPLv3
 * PURPOSE:      Synchronization API types shared by kernel and user mode.
 */

#ifndef MATANELOS_SHARED_SYNCHAPI_H
#define MATANELOS_SHARED_SYNCHAPI_H

#include <stdbool.h>
#include <stdint.h>

/* WaitForSingleObject and WaitForSingleObjectEx return values. */
#define WAIT_OBJECT_0      ((uint32_t)0x00000000UL)
#define WAIT_ABANDONED_0   ((uint32_t)0x00000080UL)
#define WAIT_ABANDONED     WAIT_ABANDONED_0
#define WAIT_TIMEOUT       ((uint32_t)0x00000102UL)
#define WAIT_FAILED        ((uint32_t)0xFFFFFFFFUL)

/*
 * Event behavior is part of the public ABI. Keep these values stable: a
 * notification event remains signaled until reset, while a synchronization
 * event releases one waiter and is then reset automatically.
 */
typedef enum _EVENT_TYPE {
    NotificationEvent = 0,
    SynchronizationEvent = 1
} EVENT_TYPE;

typedef struct _MUTEX_BASIC_INFORMATION {
    int32_t SignalState;
    bool OwnedByCaller;
    bool Abandoned;
} MUTEX_BASIC_INFORMATION, *PMUTEX_BASIC_INFORMATION;

typedef struct _SEMAPHORE_BASIC_INFORMATION {
    int32_t CurrentCount;
    int32_t MaximumCount;
} SEMAPHORE_BASIC_INFORMATION, *PSEMAPHORE_BASIC_INFORMATION;

#endif /* MATANELOS_SHARED_SYNCHAPI_H */
