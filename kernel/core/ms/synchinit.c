#include "../../includes/ms.h"
#include "../../includes/ob.h"
#include "../../includes/ps.h"
#include "../../assert.h"

POBJECT_TYPE MsEventType;
POBJECT_TYPE MsMutexType;
POBJECT_TYPE MsSemaphoreType;

static void
MspDeleteSynchronizationObject(
    IN void* Object
)

/*++

    Routine description:

        Releases object-specific state when a dispatcher object is deleted.

    Arguments:

        [IN OUT] Object - Object affected by the operation.

    Return Values:

        None.

--*/

{
    PDISPATCHER_HEADER Header = (PDISPATCHER_HEADER)Object;
    assert(Header != NULL);
    assert(IsListEmpty(&Header->WaitListHead));

    if (Header->Type == DispatcherMutex) {
        PMUTEX Mutex = (PMUTEX)Object;
        assert(Mutex->OwnerThread == NULL);
        assert(IsListEmpty(&Mutex->OwnerListEntry));
        assert(Mutex->ObjectOwnerReferences == 0);
        (void)Mutex;
    }
}

MTSTATUS
MsInitializeSynchronization(
	void
)

/*++

    Routine description:

        Initializes dispatcher object types and synchronization support.

    Arguments:

        None.

    Return Values:

        MT_SUCCESS on success, or an error status describing the failure.

--*/

{
    // Define how each mutex & events be created.
    MTSTATUS status;
    OBJECT_TYPE_INITIALIZER ObjectTypeInitializer;
    kmemset(&ObjectTypeInitializer, 0, sizeof(OBJECT_TYPE_INITIALIZER));

    // Mutexes
    const char* Name = "Mutex";
    ObjectTypeInitializer.PoolType = NonPagedPool;
#ifdef DEBUG
    ObjectTypeInitializer.DumpProcedure = NULL; // TODO DUMP PROC!
#else
    ObjectTypeInitializer.DumpProcedure = NULL;
#endif
    ObjectTypeInitializer.DeleteProcedure = MspDeleteSynchronizationObject;
    ObjectTypeInitializer.ValidAccessRights = MT_MUTEX_ALL_ACCESS;
    status = ObCreateObjectType(Name, &ObjectTypeInitializer, &MsMutexType);
    if (MT_FAILURE(status)) return status;

    // Events
    Name = "Event";
    ObjectTypeInitializer.PoolType = NonPagedPool;
#ifdef DEBUG
    ObjectTypeInitializer.DumpProcedure = NULL; // TODO DUMP PROC!
#else
    ObjectTypeInitializer.DumpProcedure = NULL;
#endif
    ObjectTypeInitializer.DeleteProcedure = MspDeleteSynchronizationObject;
    ObjectTypeInitializer.ValidAccessRights = MT_EVENT_ALL_ACCESS;
    status = ObCreateObjectType(Name, &ObjectTypeInitializer, &MsEventType);
    if (MT_FAILURE(status)) return status;

    // Semaphores
    Name = "Semaphore";
    ObjectTypeInitializer.PoolType = NonPagedPool;
#ifdef DEBUG
    ObjectTypeInitializer.DumpProcedure = NULL; // TODO DUMP PROC!
#else
    ObjectTypeInitializer.DumpProcedure = NULL;
#endif
    ObjectTypeInitializer.DeleteProcedure = MspDeleteSynchronizationObject;
    ObjectTypeInitializer.ValidAccessRights = MT_SEMAPHORE_ALL_ACCESS;
    status = ObCreateObjectType(Name, &ObjectTypeInitializer, &MsSemaphoreType);
    if (MT_FAILURE(status)) return status;

    // Initialize the timer list head.
    MsTimerQueueLock.locked = 0;
    InitializeListHead(&MsTimerQueue);

    return status;
}
